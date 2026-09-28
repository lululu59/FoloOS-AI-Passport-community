import contextlib
import hashlib
import io
import itertools
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.firmware_catalog import (
    OPTIONAL_APPS, SLOT_BYTES, install_variant, load_catalog, main, select_variant,
)


class FirmwareCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.catalog = {"schema": 1, "target": "esp32c3", "slot_bytes": SLOT_BYTES,
                        "variants": []}
        for index, flags in enumerate(itertools.product((False, True), repeat=2)):
            apps = [app for app, enabled in zip(OPTIONAL_APPS, flags) if enabled]
            image = bytearray(32 + index)
            image[0] = 0xE9
            image[12] = 5
            filename = f"{index}.bin"
            (self.directory / filename).write_bytes(image)
            self.catalog["variants"].append({
                "id": str(index), "apps": apps, "file": filename,
                "size_bytes": len(image), "sha256": hashlib.sha256(image).hexdigest(),
            })
        self.save()

    def save(self):
        (self.directory / "manifest.json").write_text(json.dumps(self.catalog))

    def test_all_four_choices_resolve_to_the_exact_image(self):
        catalog = load_catalog(self.directory)
        for variant in catalog["variants"]:
            self.assertEqual(select_variant(catalog, list(reversed(variant["apps"]))), variant)

    def test_missing_combination_never_falls_back_to_full_image(self):
        self.catalog["variants"].pop(0)
        with self.assertRaises(ValueError):
            select_variant(self.catalog, [])

    def test_corrupted_image_is_rejected_before_any_network_operation(self):
        (self.directory / "0.bin").write_bytes(b"corrupted")
        with patch("tools.mac_bridge.wireless_ota_update") as update:
            with self.assertRaises(ValueError):
                install_variant(self.directory, [])
            update.assert_not_called()

    def test_correct_image_is_passed_to_existing_ota_transport(self):
        with patch("tools.mac_bridge.wireless_ota_update") as update:
            with contextlib.redirect_stdout(io.StringIO()):
                install_variant(self.directory, ["pomodoro"])
            update.assert_called_once_with(self.directory / "2.bin")

    def test_hash_size_chip_and_slot_are_checked(self):
        for field, value in (("sha256", "0" * 64), ("size_bytes", 1234)):
            with self.subTest(field=field):
                original = self.catalog["variants"][0][field]
                self.catalog["variants"][0][field] = value
                self.save()
                with self.assertRaises(ValueError):
                    load_catalog(self.directory)
                self.catalog["variants"][0][field] = original
        self.save()
        image = bytearray((self.directory / "0.bin").read_bytes())
        image[12] = 0  # ESP32 instead of ESP32-C3, even with a matching hash.
        (self.directory / "0.bin").write_bytes(image)
        self.catalog["variants"][0]["sha256"] = hashlib.sha256(image).hexdigest()
        self.save()
        with self.assertRaises(ValueError):
            load_catalog(self.directory)
        self.catalog["slot_bytes"] = 0x400000
        self.save()
        with self.assertRaises(ValueError):
            load_catalog(self.directory)

    def test_oversized_image_is_rejected(self):
        image = bytearray(SLOT_BYTES + 1)
        image[0], image[12] = 0xE9, 5
        (self.directory / "0.bin").write_bytes(image)
        self.catalog["variants"][0].update(size_bytes=len(image), sha256=hashlib.sha256(image).hexdigest())
        self.save()
        with self.assertRaises(ValueError):
            load_catalog(self.directory)

    def test_manifest_cannot_escape_bundle_directory(self):
        self.catalog["variants"][0]["file"] = "../outside.bin"
        self.save()
        with self.assertRaises(ValueError):
            load_catalog(self.directory)

    def test_malformed_manifest_is_reported_without_installing(self):
        with patch("tools.mac_bridge.wireless_ota_update") as update:
            for value in ([], {"schema": 999}, {**self.catalog, "variants": [None]}):
                with self.subTest(value=value):
                    (self.directory / "manifest.json").write_text(json.dumps(value))
                    with contextlib.redirect_stderr(io.StringIO()):
                        self.assertEqual(main([
                            "--directory", str(self.directory), "--install", "--apps",
                        ]), 1)
            update.assert_not_called()

    def test_duplicate_and_unknown_choices_are_rejected(self):
        for apps in (["unknown"], ["pomodoro", "pomodoro"]):
            with self.assertRaises(ValueError):
                select_variant(self.catalog, apps)
        self.catalog["variants"].append(self.catalog["variants"][0])
        self.save()
        with self.assertRaises(ValueError):
            load_catalog(self.directory)

    def test_catalog_inspection_does_not_install(self):
        with patch("tools.mac_bridge.wireless_ota_update") as update:
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["--directory", str(self.directory), "--catalog"]), 0)
            self.assertEqual(json.loads(output.getvalue()), self.catalog)
            update.assert_not_called()

    def test_install_requires_explicit_choices(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(["--directory", str(self.directory), "--install"]), 1)


if __name__ == "__main__":
    unittest.main()
