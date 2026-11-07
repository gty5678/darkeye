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
        payload = self.root / "out/update-payload"
        (payload / "bin").mkdir(parents=True)
        executable = payload / "bin/Darkeye"
        executable.write_bytes(b"program")
        executable.chmod(0o755)
        (payload / "bin/current").symlink_to("Darkeye")
        archive = self.root / "out/Darkeye-1.2.5-linux-x64-update.tar.zst"
        package.write_update_package(payload, archive, "linux", "x64", "1.2.5")
        with tarfile.open(fileobj=io.BytesIO(zstandard.ZstdDecompressor().stream_reader(archive.read_bytes()).read()), mode="r:") as z:
            self.assertEqual(set(z.getnames()), {"bin", "bin/Darkeye", "bin/current"})
            self.assertEqual(z.getmember("bin/Darkeye").mode & 0o111, 0o111)
            self.assertEqual(z.getmember("bin/current").linkname, "Darkeye")
            self.assertTrue(z.getmember("bin/current").issym())
        manifest = json.loads((archive.parent / "latest.json").read_text())
        entry = manifest["packages"]["linux-x64"]
        self.assertEqual(entry["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(entry["url"], f"https://darkeye.win/releases/{archive.name}")
        (payload / "bin/portable.ini").write_text("user config")
        with self.assertRaises(RuntimeError):
            package.write_update_package(payload, archive, "linux", "x64", "1.2.5")

    def test_update_layout_for_each_platform(self):
        for system, arch, entry in [("windows", "x64", "Darkeye.exe"),
                                    ("linux", "x64", "bin/Darkeye"),
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
        payload = self.root / "out/payload"
        payload.mkdir()
        archive = self.root / "out/update.tar.zst"
        archive.write_bytes(b"previous release")
        link = payload / "link"
        for target in (self.root, payload):
            link.symlink_to(target, target_is_directory=True)
            with self.assertRaises(RuntimeError):
                package.write_update_package(payload, archive, "linux", "x64", "1.2.5")
            self.assertEqual(archive.read_bytes(), b"previous release")
            link.unlink()

    def test_supported_native_architectures(self):
        self.assertEqual(package.release_platform("Windows", "AMD64"), ("windows", "x64"))
        self.assertEqual(package.release_platform("Linux", "x86_64"), ("linux", "x64"))
        self.assertEqual(package.release_platform("Darwin", "arm64"), ("macos", "arm64"))
        for system, arch in [("Darwin", "x86_64"), ("Linux", "arm64"), ("Windows", "ARM64")]:
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

    def test_linux_portable_archive_layout_and_checksums(self):
        destination = self.root / "out/payload"
        resources = destination / "share/darkeye"
        for relative in ["bin/Darkeye", "bin/updater", "plugins/platforms/libqxcb.so", "share/darkeye/config/update.ini", "share/darkeye/LICENSE"]:
            file = destination / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"payload")
        (resources / "avwiki").mkdir()
        (resources / "avwiki/page.md").write_text("seed")
        base = self.root / "out/Darkeye-1.2.5-linux-x64"
        outputs = package.package_linux(SimpleNamespace(format="portable", dry_run=False), destination,
                                        self.root / "out/portable", base, "1.2.5", os.environ.copy())
        self.assertEqual(len(outputs), 1)
        with tarfile.open(outputs[0], "r:gz") as archive:
            names = archive.getnames()
            self.assertIn("DarkEye/data", names)
            self.assertIn("DarkEye/app-1.2.5/bin/updater", names)
            marker = archive.extractfile("DarkEye/app-1.2.5/bin/portable.ini").read()
            self.assertIn(b"DataDirectory=../../data", marker)
            launcher = archive.getmember("DarkEye/Darkeye.sh")
            self.assertEqual(launcher.mode & 0o111, 0o111)
            self.assertIn(b'app-1.2.5/bin/Darkeye', archive.extractfile(launcher).read())
        self.assertFalse((destination / "bin/portable.ini").exists())
        package.write_checksums(outputs)
        self.assertFalse(Path(str(outputs[0]) + ".sha256").exists())

    def test_only_update_package_has_checksum(self):
        update = self.root / "out/Darkeye-1.2.5-linux-x64-update.tar.zst"
        update.write_bytes(b"compressed update")
        portable = self.root / "out/portable.zip"
        portable.write_bytes(b"portable")
        package.write_checksums([portable, update])
        self.assertFalse(Path(str(portable) + ".sha256").exists())
        self.assertEqual(Path(str(update) + ".sha256").read_text().split()[0],
                         hashlib.sha256(update.read_bytes()).hexdigest())

    def test_successful_release_keeps_only_requested_artifacts(self):
        base = self.root / "out/Darkeye-1.2.5-macos-arm64"
        destination = base.with_name(base.name + "-install")
        portable = base.with_name(base.name + "-portable")
        outputs = [base.with_name(base.name + suffix) for suffix in
                   ("-portable.zip", "-setup.dmg")]
        for suffix in ("-setup.pkg", "-setup.pkg.sha256", "-portable.zip.sha256",
                       "-install-components.plist"):
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
            base.name + "-update.tar.zst", base.name + "-update.tar.zst.sha256", "latest.json"})

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
        with patch.object(package, "run") as run:
            outputs = package.package_linux(args, self.root / "out/payload", self.root / "out/portable",
                                            self.root / "out/Darkeye-1.2.5-linux-x64", "1.2.5", os.environ.copy())
        self.assertTrue(str(outputs[0]).endswith("-portable.tar.gz"))
        self.assertTrue(str(outputs[1]).endswith("-setup.deb"))
        self.assertIn("--root-owner-group", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
