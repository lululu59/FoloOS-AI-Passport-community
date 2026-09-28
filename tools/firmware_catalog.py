"""Select verified, prebuilt application combinations for the Mac installer."""

import argparse
import hashlib
import json
from pathlib import Path
import sys


SLOT_BYTES = 0x300000
OPTIONAL_APPS = ("pomodoro", "word_bear")


def validate_image(image: bytes) -> None:
    # esp_image_header_t: magic at 0, chip_id (ESP32-C3 = 5) at 12.
    if len(image) < 24 or image[0] != 0xE9 or image[12:14] != b"\x05\x00":
        raise ValueError("固件不是 ESP32-C3 应用镜像")
    if len(image) > SLOT_BYTES:
        raise ValueError("固件超过 3 MiB 应用分区")


def load_catalog(directory: Path) -> dict:
    directory = directory.resolve()
    catalog = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if (not isinstance(catalog, dict) or catalog.get("schema") != 1 or catalog.get("target") != "esp32c3"
            or catalog.get("slot_bytes") != SLOT_BYTES):
        raise ValueError("固件目录版本、芯片或分区大小不匹配")
    variants = catalog.get("variants")
    if not isinstance(variants, list) or not variants:
        raise ValueError("安装包中没有可用固件，请重新构建完整安装包")
    seen = set()
    for variant in variants:
        if not isinstance(variant, dict):
            raise ValueError("固件条目无效")
        apps = variant["apps"]
        if (not isinstance(apps, list) or any(app not in OPTIONAL_APPS for app in apps)
                or len(apps) != len(set(apps))):
            raise ValueError("固件应用列表无效")
        key = tuple(sorted(apps))
        if key in seen:
            raise ValueError("固件应用组合重复")
        seen.add(key)
        path = (directory / variant["file"]).resolve()
        if path.parent != directory or path.suffix != ".bin":
            raise ValueError("固件路径无效")
        image = path.read_bytes()
        validate_image(image)
        if (len(image) != variant["size_bytes"]
                or hashlib.sha256(image).hexdigest() != variant["sha256"]):
            raise ValueError("固件校验失败，请重新构建或获取完整安装包")
    return catalog


def select_variant(catalog: dict, apps: list[str]) -> dict:
    if len(apps) != len(set(apps)) or any(app not in OPTIONAL_APPS for app in apps):
        raise ValueError("未知或重复的应用选项")
    for variant in catalog["variants"]:
        if set(variant["apps"]) == set(apps):
            return variant
    raise ValueError("安装包不包含所选组合，不能用其他固件代替")


def install_variant(directory: Path, apps: list[str]) -> None:
    catalog = load_catalog(directory)
    variant = select_variant(catalog, apps)
    # Import only for installation: browsing the catalog never connects to hardware.
    if __package__:
        from .mac_bridge import wireless_ota_update
    else:
        from mac_bridge import wireless_ota_update
    print(f"所选固件：{variant['id']}；{variant['size_bytes'] / 1024:.1f} KiB", flush=True)
    wireless_ota_update(directory / variant["file"])


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--catalog", action="store_true")
    mode.add_argument("--install", action="store_true")
    parser.add_argument("--apps", nargs="*", choices=OPTIONAL_APPS)
    args = parser.parse_args(argv)
    try:
        if args.catalog:
            print(json.dumps(load_catalog(args.directory), ensure_ascii=False))
        else:
            if args.apps is None:
                raise ValueError("安装时必须明确指定 --apps（可为空）")
            install_variant(args.directory, args.apps)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f"无法安装自定义固件：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
