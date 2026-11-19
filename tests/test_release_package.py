"""Offline regression tests for the Skell Delay Windows VST3 release gate."""
import importlib.util
import json
import pathlib
import struct
import tempfile
import shutil
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "verify_windows_vst3.py"
SPEC = importlib.util.spec_from_file_location("verify_windows_vst3", SCRIPT)
GATE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(GATE)


def pe(machine=0x8664, magic=0x20B, signature=b"PE\0\0", sections=1, dll=True):
    data = bytearray(1024)
    data[:2] = b"MZ"
    off = 0x80
    struct.pack_into("<I", data, 0x3C, off)
    data[off:off + 4] = signature
    struct.pack_into("<HH", data, off + 4, machine, sections)
    struct.pack_into("<HH", data, off + 20, 0xF0, 0x2022 if dll else 0x0022)
    struct.pack_into("<H", data, off + 24, magic)
    return bytes(data)


def metadata(product="Skell-Delay", vendor="Skelly Audio", version="1.0.0",
             component="ABCDEF019182FAEB536B6C79446C6179",
             controller="ABCDEF011234ABCD536B6C79446C6179"):
    return {
        "Name": product, "Version": version,
        "Factory Info": {"Vendor": vendor, "URL": "", "E-Mail": "",
                         "Description": 'comma ,} inside string, quote "kept"'},
        "Classes": [
            {"CID": component, "Category": "Audio Module Class", "Name": product,
             "Vendor": vendor, "Version": version},
            {"CID": controller, "Category": "Component Controller Class", "Name": product,
             "Vendor": vendor, "Version": version},
        ],
    }


def trailing_commas(text):
    out, quoted, escaped = [], False, False
    for ch in text:
        if quoted:
            out.append(ch)
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == '"': quoted = False
        elif ch == '"':
            quoted = True
            out.append(ch)
        elif ch in "}]":
            out.extend([",", ch])
        else:
            out.append(ch)
    return "".join(out)


def render(**kwargs):
    return trailing_commas(json.dumps(metadata(**kwargs), indent=2))


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.cmake = self.root / "CMakeLists.txt"
        self.cmake.write_text(
            'project(DelayPlugin VERSION 1.0.0 LANGUAGES C CXX)\n'
            'juce_add_plugin(DelayPlugin\n'
            '  COMPANY_NAME "Skelly Audio"\n'
            '  PRODUCT_NAME "Skell-Delay"\n'
            '  FORMATS VST3\n'
            ')\n',
            encoding="utf-8")
        self.bundle = self.root / "build/VST3/Skell-Delay.vst3"
        self.module = self.bundle / GATE.MODULE
        self.info = self.bundle / GATE.INFO
        self.module.parent.mkdir(parents=True)
        self.info.parent.mkdir(parents=True)
        self.module.write_bytes(pe())
        self.info.write_text(render(), encoding="utf-8")
        self.juce_source = self.root / "juce-src"
        sdk_license = self.juce_source / "modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"
        sdk_license.parent.mkdir(parents=True)
        (self.juce_source / "LICENSE.md").write_text("Pinned JUCE license fixture. " + "x" * 300, encoding="utf-8")
        sdk_license.write_text("Pinned VST3 SDK license fixture. " + "y" * 300, encoding="utf-8")

    def run_stage(self):
        package = self.root / "package"
        if package.exists():
            shutil.rmtree(package)
        return GATE.stage(self.bundle, package, self.cmake,
                          "1" * 40, "2" * 40, "43999999999", 10,
                          juce_source=self.juce_source)

    def test_cmake_release_target_is_windows_vst3_only(self):
        original = self.cmake.read_text(encoding="utf-8")
        self.assertEqual(GATE.identity_from_cmake(self.cmake)["version"], "1.0.0")
        for formats in ("VST3 AU", "VST3 Standalone", "VST3 VST2"):
            with self.subTest(formats=formats):
                self.cmake.write_text(original.replace("FORMATS VST3", f"FORMATS {formats}"), encoding="utf-8")
                with self.assertRaisesRegex(GATE.PackageError, "VST3 only"):
                    GATE.identity_from_cmake(self.cmake)

    def test_normalizes_only_trailing_commas_outside_strings(self):
        src = trailing_commas(json.dumps({"s": 'comma ,} and "quote"', "a": [1, 2]}))
        self.assertEqual(json.loads(GATE.strip_trailing_commas(src))["s"], 'comma ,} and "quote"')

    def test_realistic_bundle_stages_manifest_and_keeps_source_unchanged(self):
        before = self.info.read_bytes()
        result = self.run_stage()
        self.assertEqual(result["release_status"], "preview-only")
        self.assertTrue(result["moduleinfo"]["normalization_applied"])
        self.assertEqual(self.info.read_bytes(), before)
        staged_info = self.root / "package/Skell-Delay.vst3" / GATE.INFO
        json.loads(staged_info.read_text(encoding="utf-8"))
        self.assertEqual(len((self.root / "package/SHA256SUMS.txt").read_text().splitlines()), len(result["bundle_files"]) + 6)
        self.assertTrue((self.root / "package/Install-SkellDelayPreview.ps1").is_file())
        self.assertTrue((self.root / "package/INSTALL_PREVIEW.md").is_file())
        self.assertIn("NOT FOR REDISTRIBUTION OR SALE", (self.root / "package/THIRD_PARTY_NOTICES.txt").read_text())
        self.assertTrue((self.root / "package/ThirdPartyLicenses/JUCE-LICENSE.md").is_file())
        self.assertTrue((self.root / "package/ThirdPartyLicenses/VST3-SDK-LICENSE.txt").is_file())
        self.assertFalse((self.root / "package/EULA.txt").exists())
        self.assertEqual(result["legal"]["status"], "not-approved-for-distribution")
        self.assertEqual(result["installer"]["script"]["path"], "Install-SkellDelayPreview.ps1")

    def test_checksum_manifest_covers_every_bundle_and_distribution_file(self):
        result = self.run_stage()
        package = self.root / "package"
        entries = {}
        for line in (package / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
            digest, relative = line.split("  ", 1)
            self.assertNotIn(relative, entries)
            entries[relative] = digest
        expected = {item["path"] for item in result["bundle_files"]}
        expected.update({"Install-SkellDelayPreview.ps1", "INSTALL_PREVIEW.md", "THIRD_PARTY_NOTICES.txt", "ThirdPartyLicenses/JUCE-LICENSE.md", "ThirdPartyLicenses/VST3-SDK-LICENSE.txt", "build-info.json"})
        self.assertEqual(set(entries), expected)
        for relative, digest in entries.items():
            self.assertEqual(GATE.sha((package / relative).read_bytes()), digest)

    def test_already_strict_json_is_not_rewritten(self):
        self.info.write_text(json.dumps(metadata()), encoding="utf-8")
        self.assertFalse(self.run_stage()["moduleinfo"]["normalization_applied"])

    def test_duplicate_json_keys_fail(self):
        self.info.write_text('{"Name":"Skell-Delay","Name":"Other"}', encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_non_finite_json_numbers_fail_without_rewriting_source(self):
        # Python's json module accepts NaN and +/-Infinity unless parse_constant
        # is overridden. JUCE module metadata must remain standards-compliant JSON.
        for token in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(token=token):
                raw = trailing_commas(
                    json.dumps(metadata()).replace('"URL": ""', f'"URL": {token}'))
                self.info.write_text(raw, encoding="utf-8")
                before = self.info.read_bytes()
                with self.assertRaisesRegex(
                    GATE.PackageError, "Non-finite JSON numeric constant is forbidden"
                ):
                    self.run_stage()
                # A rejected package must never be normalized in place.
                self.assertEqual(self.info.read_bytes(), before)

    def test_invalid_json_fails(self):
        self.info.write_text('{"Name":"Skell-Delay","Classes":[,]}', encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_product_vendor_and_version_must_match(self):
        for kwargs in ({"product": "Other"}, {"vendor": "Other Audio"}, {"version": "2.0.0"}):
            with self.subTest(kwargs=kwargs):
                self.info.write_text(render(**kwargs), encoding="utf-8")
                with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_class_ids_and_categories_must_be_valid_and_unique(self):
        self.info.write_text(render(controller="ABCDEF019182FAEB536B6C79446C6179"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()
        self.info.write_text(render(controller="bad"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()
        self.info.write_text(render().replace("Component Controller Class", "Unknown"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_pe_must_be_amd64_pe32_plus_dll(self):
        for image in (pe(machine=0x14C), pe(magic=0x10B), pe(signature=b"PE\\\\0\\\\0"),
                      pe(sections=0), pe(dll=False)):
            with self.subTest(image=image[128:154]):
                self.module.write_bytes(image)
                with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_rejects_extra_module_and_non_vst3_payloads(self):
        (self.bundle / "extra.vst3").write_bytes(pe())
        with self.assertRaises(GATE.PackageError): self.run_stage()
        (self.bundle / "extra.vst3").unlink()
        (self.bundle / "unexpected.exe").write_bytes(b"MZ")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_rejects_multiple_bundles_and_stale_staging(self):
        (self.bundle.parent / "Other.vst3").mkdir()
        with self.assertRaises(GATE.PackageError): self.run_stage()
        (self.bundle.parent / "Other.vst3").rmdir()
        package = self.root / "package"
        package.mkdir()
        (package / "stale.txt").write_text("stale")
        with self.assertRaises(GATE.PackageError):
            GATE.stage(self.bundle, package, self.cmake, "1" * 40, "2" * 40, "439", 10)

    def test_public_release_flag_rejects_unapproved_legal_record(self):
        with self.assertRaisesRegex(GATE.PackageError, "not APPROVED"):
            GATE.stage(self.bundle, self.root / "public-package", self.cmake,
                       "1" * 40, "2" * 40, "43999999999", 10,
                       public_release_prepared=True, juce_source=self.juce_source)

    def test_requires_full_commits_and_pluginval_ten(self):
        with self.assertRaises(GATE.PackageError):
            GATE.stage(self.bundle, self.root / "p1", self.cmake, "abc", "2"*40, "439", 10)
        with self.assertRaises(GATE.PackageError):
            GATE.stage(self.bundle, self.root / "p2", self.cmake, "1"*40, "2"*40, "439", 5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
