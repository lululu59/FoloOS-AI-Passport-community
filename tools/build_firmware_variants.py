"""Build the four offline-app combinations using an activated ESP-IDF 5.5.3."""

import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from firmware_catalog import OPTIONAL_APPS, SLOT_BYTES, load_catalog, validate_image


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build" / "firmware"


def main() -> None:
    idf = shutil.which("idf.py")
    if not idf:
        raise RuntimeError("请先激活 ESP-IDF 5.5.3 的 export.sh")
    version = subprocess.check_output([idf, "--version"], text=True)
    if not re.search(r"\bv5\.5\.3(?:\s|$)", version):
        raise RuntimeError(f"需要 ESP-IDF 5.5.3，当前为 {version.strip()}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # Never leave an apparently complete catalog after a failed partial rebuild.
    (OUTPUT / "manifest.json").unlink(missing_ok=True)
    variants = []
    for flags in itertools.product((False, True), repeat=len(OPTIONAL_APPS)):
        apps = [app for app, enabled in zip(OPTIONAL_APPS, flags) if enabled]
        variant_id = "-".join(apps) or "base"
        work = ROOT / "build" / "firmware-builds" / variant_id
        work.mkdir(parents=True, exist_ok=True)
        config = work / "sdkconfig"
        defaults = work / "sdkconfig.defaults"
        defaults.write_text((ROOT / "sdkconfig.defaults").read_text() + "\n" + "\n".join(
            f"CONFIG_FOLOOS_APP_{app.upper()}={'y' if enabled else 'n'}"
            for app, enabled in zip(OPTIONAL_APPS, flags)
        ) + "\n")
        # Each combination owns its config; never change the developer's root sdkconfig.
        config.unlink(missing_ok=True)
        print(f"构建应用组合：{variant_id}", flush=True)
        subprocess.run([
            idf, "-B", str(work), "-D", f"SDKCONFIG={config}",
            "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build",
        ], cwd=ROOT, check=True)
        actual = config.read_text()
        for app, enabled in zip(OPTIONAL_APPS, flags):
            if (f"CONFIG_FOLOOS_APP_{app.upper()}=y" in actual) != enabled:
                raise RuntimeError(f"{variant_id} 的配置与选择不一致")
        image = (work / "FoloToy-AI-Passport.bin").read_bytes()
        validate_image(image)
        filename = f"{variant_id}.bin"
        (OUTPUT / filename).write_bytes(image)
        variants.append({
            "id": variant_id, "apps": apps, "file": filename,
            "size_bytes": len(image), "sha256": hashlib.sha256(image).hexdigest(),
        })
    manifest = {"schema": 1, "target": "esp32c3", "slot_bytes": SLOT_BYTES,
                "variants": variants}
    temporary = OUTPUT / "manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    os.replace(temporary, OUTPUT / "manifest.json")
    load_catalog(OUTPUT)
    print(f"固件及容量清单已生成：{OUTPUT}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
