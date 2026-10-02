"""Exercise the shared build/CI parser without changing the repository VERSION."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CMAKE = "cmake"


class ProjectVersionTests(unittest.TestCase):
    def parse(self, version, ref="refs/heads/main", ref_name="main"):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            version_file = directory / "VERSION"
            version_file.write_text(version + "\n", encoding="utf-8")
            output_file = directory / "outputs"
            env_file = directory / "env"
            environment = dict(os.environ, GITHUB_REF=ref, GITHUB_REF_NAME=ref_name,
                               GITHUB_OUTPUT=str(output_file), GITHUB_ENV=str(env_file))
            result = subprocess.run(
                [CMAKE, f"-DMINECRAFTC_VERSION_FILE={version_file}", "-P",
                 str(ROOT / "tools/ci_version.cmake")],
                env=environment, text=True, capture_output=True)
            values = {}
            for file in (output_file, env_file):
                if file.exists():
                    values.update(line.split("=", 1) for line in file.read_text().splitlines())
            return result, values

    def valid(self, version):
        result, values = self.parse(version)
        self.assertEqual(result.returncode, 0, result.stderr)
        return values

    def test_legacy_and_numbered_channels(self):
        for channel, label in (("alpha", "Alpha"), ("beta", "Beta"),
                               ("rc", "RC"), ("release", "Release")):
            versions = [f"1.3.4-{channel}"]
            if channel != "release":
                versions += [f"1.3.4-{channel}.1", f"1.3.4-{channel}.999"]
            for version in versions:
                with self.subTest(version=version):
                    values = self.valid(version)
                    iteration = version.partition(channel + ".")[2]
                    display_label = label + ("." + iteration if iteration else "")
                    self.assertEqual(values["MINECRAFTC_VERSION_CORE"], "1.3.4")
                    self.assertEqual(values["MINECRAFTC_VERSION_DISPLAY"],
                                     display_label + "-1.3.4")
                    self.assertEqual(values["version"], version)
                    self.assertEqual(values["channel"], channel)
                    self.assertEqual(values["prerelease"],
                                     "false" if channel == "release" else "true")
                    self.assertEqual(values["release_tag"], "v" + version)
                    self.assertEqual(values["RELEASE_DIR"], "MinecraftC-" + version)

    def test_package_codes_increase_across_iterations_stages_and_core_versions(self):
        versions = ["1.3.4-alpha", "1.3.4-alpha.1", "1.3.4-alpha.999",
                    "1.3.4-beta", "1.3.4-beta.1", "1.3.4-beta.2", "1.3.4-beta.999",
                    "1.3.4-rc", "1.3.4-rc.1", "1.3.4-rc.999", "1.3.4-release",
                    "1.3.5-alpha.1", "1.4.0-alpha.1", "2.0.0-alpha.1"]
        codes = [int(self.valid(version)["MINECRAFTC_VERSION_CODE"]) for version in versions]
        self.assertTrue(all(a < b for a, b in zip(codes, codes[1:])))
        self.assertEqual(int(self.valid("1.3.4-beta.2")["MINECRAFTC_VERSION_CODE"]),
                         103042002)
        # New metadata also upgrades over the old unnumbered installation code.
        self.assertGreater(int(self.valid("1.3.3-release")["MINECRAFTC_VERSION_CODE"]), 103034)
        self.assertLessEqual(int(self.valid("20.99.99-release")["MINECRAFTC_VERSION_CODE"]),
                             2100000000)

    def test_invalid_versions_do_not_emit_ci_metadata(self):
        for version in ("1.3.4", "v1.3.4-beta.1", "1.3.4-BETA.1", "1.3.4-beta.0",
                        "1.3.4-beta.01", "1.3.4-beta.1000", "1.3.4-beta.-1",
                        "1.3.4-beta.1.2", "1.3.4-release.1", "01.3.4-beta.1",
                        "1.100.4-beta.1", "1.3.100-beta.1", "21.0.0-release",
                        "999999999999999999999.0.0-beta.1",
                        "1.3.4-beta.999999999999999999999", "1.3.4-beta;other",
                        "1.3.4-beta\n1.3.4-release", ""):
            with self.subTest(version=version):
                result, values = self.parse(version)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(values, {})

    def test_exact_release_tag_and_iteration_mismatch(self):
        for version in ("1.3.4-beta.2", "1.3.4-release", "1.3.4-rc"):
            result, values = self.parse(version, "refs/tags/v" + version, "v" + version)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(values["release_tag"], "v" + version)
        for tag in ("v1.3.4-beta.1", "v1.3.4-beta", "v1.3.4-release", "vgarbage"):
            result, values = self.parse("1.3.4-beta.2", "refs/tags/" + tag, tag)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must exactly match VERSION", result.stderr)
            self.assertEqual(values, {})

    def test_crlf_version_files(self):
        values = self.valid("1.3.4-beta.2\r")
        self.assertEqual(values["version"], "1.3.4-beta.2")

    def test_ios_template_uses_shared_core_and_build_number(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            version_file = directory / "VERSION"
            version_file.write_text("1.3.4-beta.2\n")
            plist = directory / "Info.plist"
            script = directory / "bundle.cmake"
            script.write_text(
                f'include("{(ROOT / "tools/project_version.cmake").as_posix()}")\n'
                'set(MINECRAFTC_IOS_BUILD_NUMBER "${MINECRAFTC_VERSION_CODE}")\n'
                f'configure_file("{(ROOT / "ios/Info.plist.in").as_posix()}" "{plist.as_posix()}" @ONLY)\n')
            subprocess.run([CMAKE, f"-DMINECRAFTC_VERSION_FILE={version_file}",
                            "-P", str(script)], check=True, capture_output=True)
            import plistlib
            metadata = plistlib.loads(plist.read_bytes())
            self.assertEqual(metadata["CFBundleShortVersionString"], "1.3.4")
            self.assertEqual(metadata["CFBundleVersion"], "103042002")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", default="cmake")
    args, remaining = parser.parse_known_args()
    CMAKE = args.cmake
    unittest.main(argv=[__file__, *remaining])
