"""Host-independent release recipe, archive and data-preservation tests."""
import hashlib
import json
import io
import zstandard
import os
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import package


class PackagingTest(unittest.TestCase):
    def test_dmg_retries_resource_busy_then_succeeds(self):
        command = ["hdiutil", "create", "-ov", "output.dmg"]
        busy = package.subprocess.CompletedProcess(command, 1, "", "hdiutil: create failed - Resource busy\n")
        success = package.subprocess.CompletedProcess(command, 0, "created\n", "")
        with patch.object(package.subprocess, "run", side_effect=[busy, success]) as run, \
             patch.object(package.time, "sleep") as sleep:
            package.create_macos_dmg(command, {})
        self.assertEqual(run.call_count, 2)
        sleep.assert_called_once_with(3)

    def test_dmg_does_not_retry_other_errors(self):
        command = ["hdiutil", "create"]
        failure = package.subprocess.CompletedProcess(command, 1, "", "Permission denied")
        with patch.object(package.subprocess, "run", return_value=failure) as run, \
             patch.object(package.time, "sleep") as sleep:
            with self.assertRaises(package.subprocess.CalledProcessError):
                package.create_macos_dmg(command, {})
        self.assertEqual(run.call_count, 1)
        sleep.assert_not_called()

    def test_dmg_busy_retries_are_bounded(self):
        command = ["hdiutil", "create"]
        busy = package.subprocess.CompletedProcess(command, 1, "", "Resource busy")
        with patch.object(package.subprocess, "run", return_value=busy) as run, \
             patch.object(package.time, "sleep") as sleep:
            with self.assertRaises(package.subprocess.CalledProcessError):
                package.create_macos_dmg(command, {})
        self.assertEqual(run.call_count, 3)
        self.assertEqual(sleep.call_count, 2)

    def setUp(self):
        output = package.PROJECT_ROOT / "out"
        output.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="packaging-tests-", dir=output)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "out").mkdir()
        self.context = patch.object(package, "PROJECT_ROOT", self.root)
        self.context.start()
        self.addCleanup(self.context.stop)

    def test_update_tar_zst_is_common_program_payload_with_platform_manifest(self):
        payload = self.root / "out/update-payload/Darkeye.app"
        (payload / "Contents/MacOS").mkdir(parents=True)
        executable = payload / "Contents/MacOS/Darkeye"
        executable.write_bytes(b"program")
        executable.chmod(0o755)
        (payload / "Contents/MacOS/current").symlink_to("Darkeye")
        archive = self.root / "out/Darkeye-1.2.5-macos-arm64-update.tar.zst"
        package.write_update_package(payload.parent, archive, "macos", "arm64", "1.2.5")
        with tarfile.open(fileobj=io.BytesIO(zstandard.ZstdDecompressor().stream_reader(archive.read_bytes()).read()), mode="r:") as z:
            self.assertEqual(set(z.getnames()), {"Darkeye.app", "Darkeye.app/Contents", "Darkeye.app/Contents/MacOS", "Darkeye.app/Contents/MacOS/Darkeye", "Darkeye.app/Contents/MacOS/current"})
            # Windows chmod cannot set Unix executable bits on the source file.
            if os.name == "posix":
                self.assertEqual(z.getmember("Darkeye.app/Contents/MacOS/Darkeye").mode & 0o111, 0o111)
            self.assertEqual(z.getmember("Darkeye.app/Contents/MacOS/current").linkname, "Darkeye")
            self.assertTrue(z.getmember("Darkeye.app/Contents/MacOS/current").issym())
        manifest = json.loads((archive.parent / "latest.json").read_text())
        entry = manifest["packages"]["macos-arm64"]
        self.assertEqual(entry["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(entry["url"], f"https://darkeye.win/releases/{archive.name}")
        (payload / "Contents/MacOS/portable.ini").write_text("user config")
        with self.assertRaises(RuntimeError):
            package.write_update_package(payload.parent, archive, "macos", "arm64", "1.2.5")

    def test_update_layout_for_each_platform(self):
        for system, arch, entry in [("windows", "x64", "Darkeye.exe"),
                                    ("macos", "arm64", "Contents/MacOS/Darkeye")]:
            with self.subTest(system=system):
                destination = self.root / "out" / system
                payload = destination / "Darkeye.app" if system == "macos" else destination
                executable = payload / entry
                executable.parent.mkdir(parents=True)
                executable.write_bytes(b"application")
                archive = self.root / "out" / f"{system}-update.tar.zst"
                package.write_update_package(destination, archive, system, arch, "1.2.5")
                with tarfile.open(fileobj=io.BytesIO(zstandard.ZstdDecompressor().stream_reader(archive.read_bytes()).read()), mode="r:") as z:
                    self.assertEqual([m.name for m in z.getmembers() if m.isfile()],
                                     [("Darkeye.app/" if system == "macos" else "") + entry])
                manifest = json.loads((archive.parent / "latest.json").read_text())
                self.assertEqual(manifest["latestVersion"], "1.2.5")
                self.assertIn(f"{system}-{arch}", manifest["packages"])

    def test_update_rejects_external_and_cyclic_links_without_replacing_archive(self):
        payload = self.root / "out/payload/Darkeye.app"
        payload.mkdir(parents=True)
        archive = self.root / "out/update.tar.zst"
        archive.write_bytes(b"previous release")
        link = payload / "link"
        for target in (self.root, payload):
            link.symlink_to(target, target_is_directory=True)
            with self.assertRaises(RuntimeError):
                package.write_update_package(payload.parent, archive, "macos", "arm64", "1.2.5")
            self.assertEqual(archive.read_bytes(), b"previous release")
            link.unlink()

    def test_supported_native_architectures(self):
        self.assertEqual(package.release_platform("Windows", "AMD64"), ("windows", "x64"))
        self.assertEqual(package.release_platform("Darwin", "arm64"), ("macos", "arm64"))
        for system, arch in [("Darwin", "x86_64"), ("Linux", "x86_64"), ("Linux", "arm64"), ("Windows", "ARM64")]:
            with self.assertRaises(RuntimeError):
                package.release_platform(system, arch)

    def test_staging_cleanup_preserves_user_data_and_rejects_escape(self):
        stage = self.root / "out/stage"
        profile = stage / "DarkEye/data"
        profile.mkdir(parents=True)
        sentinel = profile / "public.db"
        sentinel.write_bytes(b"user database")
        with self.assertRaises(RuntimeError):
            package.reset_output(stage)
        self.assertEqual(sentinel.read_bytes(), b"user database")
        for target in (self.root / "outside", self.root / "out/../outside"):
            with self.assertRaises(RuntimeError):
                package.reset_output(target)

    def test_successful_release_keeps_only_requested_artifacts(self):
        base = self.root / "out/Darkeye-1.2.5-macos-arm64"
        destination = base.with_name(base.name + "-install")
        portable = base.with_name(base.name + "-portable")
        outputs = [base.with_name(base.name + suffix) for suffix in
                   ("-portable.zip", "-setup.dmg")]
        for suffix in ("-setup.pkg", "-setup.pkg.sha256", "-portable.zip.sha256",
                       "-install-components.plist", "-update.tar.zst.sha256"):
            base.with_name(base.name + suffix).write_bytes(b"old")
        (base.parent / "latest-macos-arm64.json").write_text("old")

        def install(*args):
            executable = destination / "Darkeye.app/Contents/MacOS/Darkeye"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"application")

        def distribute(*args):
            (portable / "DarkEye/data").mkdir(parents=True)
            for output in outputs:
                output.write_bytes(b"distribution")
            return list(outputs)

        args = SimpleNamespace(clean=False, skip_build=True, test=False, dry_run=False,
                               destination=None, build_dir=None)
        with patch.object(package, "release_platform", return_value=("macos", "arm64")), \
             patch.object(package, "project_version", return_value="1.2.5"), \
             patch.object(package, "build_project", return_value=("cmake", {}, self.root)), \
             patch.object(package, "run", side_effect=install), \
             patch.object(package, "package_macos", side_effect=distribute):
            self.assertEqual(package.package_project(args), 0)
        self.assertEqual({p.name for p in base.parent.iterdir()}, {
            base.name + "-portable.zip", base.name + "-setup.dmg",
            base.name + "-update.tar.zst", "latest.json"})

    def test_native_installer_recipes_in_dry_run(self):
        args = SimpleNamespace(dry_run=True, format="both", mac_sign_identity=None, mac_installer_identity=None)
        base = self.root / "out/Darkeye-1.2.5-macos-arm64"
        with patch.object(package, "run") as run:
            outputs = package.package_macos(args, self.root / "out/payload", self.root / "out/portable",
                                            base, "1.2.5", "cmake", os.environ.copy())
        self.assertEqual([p.suffix for p in outputs], [".zip", ".dmg"])
        commands = [call.args[0] for call in run.call_args_list]
        self.assertTrue(any("-format" in c and "UDZO" in c for c in commands))
        self.assertTrue(any("verify" in c for c in commands))
        self.assertTrue(any("--verify" in c for c in commands))

    def test_macos_thinning_rejects_missing_arm64_before_changes(self):
        app = self.root / "Darkeye.app"
        app.mkdir()
        (app / "a-universal").write_bytes(b"\xca\xfe\xba\xbe")
        (app / "b-intel").write_bytes(b"\xcf\xfa\xed\xfe")
        with patch.object(package, "require_tool", return_value="lipo"), \
             patch.object(package.subprocess, "check_output", side_effect=["x86_64 arm64", "x86_64"]), \
             patch.object(package, "run") as run:
            with self.assertRaisesRegex(RuntimeError, "Missing arm64 slice"):
                package.thin_macos_arm64(app, os.environ.copy())
        run.assert_not_called()

    def test_macos_thinning_preserves_links_resources_and_permissions(self):
        app = self.root / "Darkeye.app"
        app.mkdir()
        binary = app / "QtCore"
        binary.write_bytes(b"\xca\xfe\xba\xbeoriginal")
        binary.chmod(0o755)
        (app / "alias").symlink_to("QtCore")
        (app / "resource").write_text("keep")
        def thin(command, env, dry_run):
            Path(command[-1]).write_bytes(b"\xcf\xfa\xed\xfethinned")
        with patch.object(package, "require_tool", return_value="lipo"), \
             patch.object(package.subprocess, "check_output", side_effect=["x86_64 arm64", "arm64"]), \
             patch.object(package, "run", side_effect=thin):
            package.thin_macos_arm64(app, os.environ.copy())
        self.assertEqual(binary.read_bytes(), b"\xcf\xfa\xed\xfethinned")
        # Unix permission preservation is exercised on macOS runners.
        if os.name == "posix":
            self.assertEqual(binary.stat().st_mode & 0o777, 0o755)
        self.assertTrue((app / "alias").is_symlink())
        self.assertEqual((app / "resource").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
