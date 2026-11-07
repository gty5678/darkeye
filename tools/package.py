#!/usr/bin/env python3
"""Build native Windows, Linux and macOS portable and installer packages."""

import argparse
import hashlib
import json
import os
from contextlib import nullcontext
import tarfile
import zipfile
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import tempfile

from build import PROJECT_ROOT, build_project, run


EXCLUDED_RUNTIME_FILES = (
    "vc_redist.x64.exe",
    "opengl32sw.dll",
    # Windows rendering uses D3D11 (RhiSceneWidget explicitly, ForceRhiCanvas
    # by QRhiWidget's default). DXC/DXIL are for D3D12 shader model 6;
    # retain d3dcompiler_47.dll for D3D11's runtime HLSL compilation.
    "dxcompiler.dll",
    "dxil.dll",
    # No TUIO/OSC touch input is enabled; native Windows touch uses qwindows.
    "plugins/generic/qtuiotouchplugin.dll",
    # Darkeye only opens QSQLITE connections; keep qsqlite.dll.
    "plugins/sqldrivers/qsqlibase.dll",
    "plugins/sqldrivers/qsqlmimer.dll",
    "plugins/sqldrivers/qsqloci.dll",
    "plugins/sqldrivers/qsqlodbc.dll",
    "plugins/sqldrivers/qsqlpsql.dll",
)
EXCLUDED_RUNTIME_DIRECTORIES = ("plugins/qmltooling",)


def project_version():
    """Read the same project version used by the application's DARKEYE_VERSION."""
    source = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    source = re.sub(r"#[^\n]*", "", source)
    match = re.search(
        r"\bproject\s*\(\s*Darkeye\b[^)]*?\bVERSION\s+"
        r"([0-9]+(?:\.[0-9]+){0,3})(?=\s|\))",
        source, re.IGNORECASE,
    )
    if not match:
        raise RuntimeError("无法读取 CMakeLists.txt 中的 project(Darkeye VERSION ...) 版本号。")
    return match.group(1)


def remove_unused_runtime(destination, dry_run=False):
    """Prune unused graphics compilers, input/SQL plugins and QML tooling."""
    destination = destination.absolute()
    root = destination.resolve()
    output_root = PROJECT_ROOT.resolve() / "out"
    if output_root.resolve() != output_root or output_root not in root.parents:
        raise RuntimeError(f"运行时清理必须位于项目 out 目录内：{destination}")
    excluded_files = [destination / name for name in EXCLUDED_RUNTIME_FILES]
    paths = list(excluded_files)
    directories = [destination / name for name in EXCLUDED_RUNTIME_DIRECTORIES]
    # .qmltypes describes types to IDE/lint tooling, not to the QML runtime.
    # Restrict matching to deployed QML; leave qmldir and executable QML intact.
    paths.extend(sorted((destination / "qml").rglob("*.qmltypes")))
    # Validate all targets before deleting any of them, including directory links.
    checks = [destination, *paths, *directories]
    for directory in directories:
        if directory.is_dir():
            checks.extend(directory.rglob("*"))
    for path in checks:
        resolved = path.resolve()
        if (resolved != root and root not in resolved.parents) or path.is_symlink() or getattr(
            path, "is_junction", lambda: False
        )():
            raise RuntimeError(f"拒绝清理链接或安装目录以外的路径：{path}")
    for path in paths:
        if path.is_file():
            print(f"移除{'（预览）' if dry_run else ''}：{path}")
            if not dry_run:
                path.unlink()
        elif dry_run and path in excluded_files:
            print(f"移除（如果存在）：{path}")
    for directory in directories:
        if directory.is_dir() or dry_run:
            print(f"移除{'（如果存在）' if dry_run else ''}：{directory}")
            if not dry_run:
                shutil.rmtree(directory)
    if dry_run:
        print(f"移除安装后的 QML 类型元数据：{destination / 'qml'}/**/*.qmltypes")


def find_seven_zip():
    for name in ("7zz", "7z", "7za"):
        executable = shutil.which(name)
        if executable:
            return executable
    for root in (Path("C:/Program Files"), Path("C:/Program Files (x86)")):
        executable = root / "7-Zip/7z.exe"
        if executable.is_file():
            return str(executable)
    raise RuntimeError("找不到 7-Zip，请安装并将 7z / 7zz 加入 PATH。")


def remove_existing_outputs(destination, archive, dry_run=False):
    """Replace only named staging outputs, never user profiles or external links."""
    paths = (destination, archive, archive.with_suffix(".7z.tmp"))
    for path in paths:
        safe_output(path)
    for path in paths:
        reset_output(path, dry_run)


def archive_install(destination, archive, seven_zip):
    """Keep one top-level directory in the 7z and test archive integrity."""
    files = sorted(path for path in destination.rglob("*") if path.is_file())
    if not files:
        raise RuntimeError("安装目录为空，无法打包。")
    temporary = archive.with_suffix(".7z.tmp")
    if temporary.exists() or archive.exists():
        raise RuntimeError(f"压缩目标已存在：{archive}")
    total_bytes = sum(path.stat().st_size for path in files)
    print(f"开始 7z 压缩：{len(files)} 个文件，"
          f"原始大小 {total_bytes / 1024 / 1024:.1f} MiB", flush=True)
    started = time.perf_counter()
    subprocess.run(
        [seven_zip, "a", "-t7z", "-mx=7", "-bsp1", str(temporary), destination.name],
        cwd=destination.parent, check=True,
    )
    print(f"压缩用时：{time.perf_counter() - started:.1f} 秒", flush=True)
    print("正在校验压缩包完整性……", flush=True)
    subprocess.run([seven_zip, "t", "-bsp1", str(temporary)], check=True)
    temporary.rename(archive)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, help="指定项目 out 内的安装目录，自动覆盖同名产物")
    parser.add_argument("--dry-run", action="store_true", help="只显示构建、安装和打包步骤")
    parser.add_argument("--format", choices=("both", "portable", "installer"), default="both")
    parser.add_argument("--iscc", type=Path, help="Inno Setup ISCC.exe path")
    parser.add_argument("--build-dir", type=Path, help="Use a specific native build directory")
    parser.add_argument("--test", action="store_true", help="Build and run tests before packaging")
    parser.add_argument("--mac-sign-identity", help="Developer ID Application identity (otherwise ad-hoc)")
    parser.add_argument("--skip-build", action="store_true", help="Use the existing Release build")
    parser.add_argument("--clean", action="store_true", help="Rebuild Release from scratch")
    args = parser.parse_args()
    started = time.perf_counter()
    try:
        return package_project(args)
    finally:
        if not args.dry_run:
            elapsed = time.perf_counter() - started
            minutes, seconds = divmod(elapsed, 60)
            print(f"总用时：{int(minutes)} 分 {seconds:.1f} 秒"
                  f"（{elapsed:.1f} 秒）", flush=True)


def find_iscc(explicit=None):
    candidates = [explicit, shutil.which("ISCC"),
                  PROJECT_ROOT / "out/tooling/inno/ISCC.exe",
                  Path("C:/Program Files (x86)/Inno Setup 6/ISCC.exe"),
                  Path("C:/Program Files/Inno Setup 7/ISCC.exe")]
    for path in candidates:
        if path and Path(path).is_file():
            return str(Path(path).absolute())
    raise RuntimeError("找不到 Inno Setup 编译器，请安装 Inno Setup 或使用 --iscc 指定 ISCC.exe。")


def package_windows(args):
    if platform.system() != "Windows":
        raise RuntimeError("此脚本生成 Windows 发布包，请在 Windows 上运行。")
    seven_zip = find_seven_zip() if args.format != "installer" else None
    iscc = find_iscc(args.iscc) if args.format != "portable" else None
    version = project_version()
    print(f"软件版本：{version}", flush=True)
    base = (args.destination or PROJECT_ROOT / "out" / f"Darkeye-{version}-windows-x64").absolute()
    destination = base.with_name(base.name + "-install")
    portable = base.with_name(base.name + "-portable")
    archive = Path(str(portable) + ".7z")
    installer = base.parent / f"Darkeye-{version}-windows-x64-setup.exe"
    remove_existing_outputs(destination, installer if iscc else Path(str(destination) + ".7z"), args.dry_run)
    if args.format != "installer":
        if (portable / "DarkEye/data").is_dir() and any((portable / "DarkEye/data").iterdir()):
            raise RuntimeError(f"Refusing to remove portable user data: {portable}")
        remove_existing_outputs(portable, archive, args.dry_run)
    cmake, env, build_dir = build_project(
        "Release", run_tests=args.test, dry_run=args.dry_run, clean=args.clean,
        skip_build=args.skip_build, build_directory=args.build_dir)
    if not args.dry_run:
        destination.mkdir(parents=True)
    run([cmake, "--install", str(build_dir), "--config", "Release",
         "--prefix", str(destination)], env, args.dry_run)
    remove_unused_runtime(destination, dry_run=args.dry_run)
    if not args.dry_run:
        for relative in ("Darkeye.exe", "updater.exe", "Qt6Core.dll", "Qt6Gui.dll",
                         "Qt6Widgets.dll", "plugins/platforms/qwindows.dll",
                         "plugins/sqldrivers/qsqlite.dll", "LICENSE", "config/update.ini"):
            if not (destination / relative).is_file():
                raise RuntimeError(f"安装缺少必要文件：{relative}")
        if any((destination / name).exists() for name in ("portable.ini", "data", "seed-data")):
            raise RuntimeError("发布目录包含便携标记或数据，请使用新的空安装暂存目录。")
        if not (destination / "avwiki").is_dir():
            raise RuntimeError("安装缺少初始知识库。")
    if args.format != "installer":
        if args.dry_run:
            print(f"便携包（DarkEye/app-{version}/portable.ini + DarkEye/data）：{archive}")
        else:
            portable_root = portable / "DarkEye"
            shutil.copytree(destination, portable_root / f"app-{version}")
            (portable_root / f"app-{version}" / "portable.ini").write_text(
                "[Portable]\nDataDirectory=../data\n", encoding="utf-8")
            (portable_root / "data").mkdir()
            write_portable_readme(portable_root, "windows")
            archive_install(portable_root, archive, seven_zip)
            print(f"便携包：{archive}")
    if args.format != "portable":
        command = [iscc, f"/DAppVersion={version}", f"/DPayloadDir={destination}",
                   f"/DOutputDir={base.parent}", str(PROJECT_ROOT / "tools/installer.iss")]
        run(command, env, args.dry_run)
        if not args.dry_run and not installer.is_file():
            raise RuntimeError(f"未生成安装包：{installer}")
        print(f"安装包：{installer}")
    return ([archive] if args.format != "installer" else []) + ([installer] if iscc else [])


def release_platform(system=None, machine=None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    if system == "Windows" and machine in ("amd64", "x86_64"):
        return "windows", "x64"
    if system == "Linux" and machine in ("amd64", "x86_64"):
        return "linux", "x64"
    if system == "Darwin" and machine in ("arm64", "aarch64"):
        return "macos", "arm64"
    raise RuntimeError(f"Unsupported native release host: {system}/{machine}")


def require_tool(name, dry_run=False):
    executable = shutil.which(name)
    if not executable and not dry_run:
        raise RuntimeError(f"Missing packaging tool: {name}")
    return executable or name


def safe_output(path):
    """Validate every existing ancestor before replacing any staging output."""
    path = path.absolute()
    output = PROJECT_ROOT.resolve() / "out"
    if output.resolve() != output or output not in path.resolve().parents:
        raise RuntimeError(f"Packaging outputs must stay inside {output}: {path}")
    current = path
    while current != output:
        if current.is_symlink() or getattr(current, "is_junction", lambda: False)():
            raise RuntimeError(f"Refusing linked output: {current}")
        current = current.parent
    return path


def reset_output(path, dry_run=False):
    path = safe_output(path)
    # Staging is replaceable; user profiles are never replaceable.
    for profile in (path / "data", path / "DarkEye/data"):
        if profile.is_dir() and any(profile.iterdir()):
            raise RuntimeError(f"Refusing to remove user data: {profile}")
    if path.is_dir():
        for child in path.rglob("*"):
            if (child.is_symlink() or getattr(child, "is_junction", lambda: False)()) and not child.resolve().is_relative_to(path.resolve()):
                raise RuntimeError(f"Output contains an external link: {child}")
    if path.exists():
        print(f"Replace staging output: {path}")
        if not dry_run:
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()


def validate_payload(destination, system):
    if system == "macos":
        resources = destination / "Darkeye.app/Contents/Resources"
        executable = destination / "Darkeye.app/Contents/MacOS/Darkeye"
        plugin = destination / "Darkeye.app/Contents/PlugIns/platforms/libqcocoa.dylib"
    else:
        resources = destination / "share/darkeye"
        executable = destination / "bin/Darkeye"
        plugin = destination / "plugins/platforms/libqxcb.so"
    for required in (executable, executable.parent / "updater", plugin,
                     resources / "config/update.ini", resources / "LICENSE"):
        if not required.is_file():
            raise RuntimeError(f"Missing deployed file: {required}")
    if not (resources / "avwiki").is_dir():
        raise RuntimeError("Missing read-only knowledge base seed")
    for root in (destination, executable.parent, resources):
        if any((root / name).exists() for name in ("portable.ini", "data", "seed-data")):
            raise RuntimeError(f"Installer payload contains user data or portable marker: {root}")
    return executable, resources


def write_portable_readme(root, system):
    text = ("Darkeye portable\n\nMove the complete DarkEye folder, including data. "
            "Keep data when replacing the application for upgrades.\n"
            "Passwords in the system credential store must be entered again on a new computer.\n")
    if system == "linux":
        text += "Run ./Darkeye.sh. Requires a compatible desktop Linux (Ubuntu 24.04 baseline), unzip and a Secret Service for saved credentials.\n"
    elif system == "macos":
        text += "Move the entire folder to a writable location before opening Darkeye.app. macOS 14.4+; Apple Silicon only.\n"
    (root / "README.txt").write_text(text, encoding="utf-8")


def package_macos(args, destination, portable, base, version, cmake, env):
    ditto = require_tool("ditto", args.dry_run)
    codesign = require_tool("codesign", args.dry_run)
    app = destination / "Darkeye.app"
    if not args.dry_run:
        executable, _ = validate_payload(destination, "macos")
        archs = subprocess.check_output([require_tool("lipo"), "-archs", str(executable)], text=True).split()
        if archs != ["arm64"]:
            raise RuntimeError(f"Expected an arm64-only application, got {archs}")
    identity = args.mac_sign_identity or "-"
    # Deployment has finished: seal nested code first, then the outer app.
    if not args.dry_run:
        nested = sorted((p for p in app.rglob("*") if not p.is_symlink() and
                         (p.suffix in (".dylib", ".so", ".framework") or
                          (p.is_file() and os.access(p, os.X_OK) and p.parent.name == "MacOS"))),
                        key=lambda p: len(p.parts), reverse=True)
    else:
        nested = []
    for item in [*nested, app]:
        command = [codesign, "--force", "--sign", identity]
        if identity != "-":
            command += ["--options", "runtime", "--timestamp"]
        run([*command, str(item)], env, args.dry_run)
    run([codesign, "--verify", "--deep", "--strict", str(app)], env, args.dry_run)
    outputs = []
    if args.format != "installer":
        archive = safe_output(base.with_name(base.name + "-portable.zip"))
        reset_output(portable, args.dry_run)
        reset_output(archive, args.dry_run)
        root = portable / "DarkEye"
        if not args.dry_run:
            root.mkdir(parents=True)
        run([ditto, str(app), str(root / "Darkeye.app")], env, args.dry_run)
        if not args.dry_run:
            (root / "portable.ini").write_text("[Portable]\nDataDirectory=data\n", encoding="utf-8")
            (root / "data").mkdir()
            write_portable_readme(root, "macos")
        run([ditto, "-c", "-k", "--sequesterRsrc", "--keepParent", str(root), str(archive)], env, args.dry_run)
        if not args.dry_run:
            with zipfile.ZipFile(archive) as package:
                if package.testzip() is not None:
                    raise RuntimeError("Portable ZIP integrity check failed")
        outputs.append(archive)
    if args.format != "portable":
        hdiutil = require_tool("hdiutil", args.dry_run)
        installer = safe_output(base.with_name(base.name + "-setup.dmg"))
        reset_output(installer, args.dry_run)
        staging = (nullcontext(str(base.with_name(base.name + "-dmg-root"))) if args.dry_run
                   else tempfile.TemporaryDirectory(prefix="dmg-", dir=base.parent))
        with staging as temporary:
            root = Path(temporary)
            run([ditto, str(app), str(root / "Darkeye.app")], env, args.dry_run)
            if not args.dry_run:
                (root / "Applications").symlink_to("/Applications", target_is_directory=True)
            run([hdiutil, "create", "-volname", "Darkeye", "-srcfolder", str(root),
                 "-format", "UDZO", "-ov", str(installer)], env, args.dry_run)
        run([hdiutil, "verify", str(installer)], env, args.dry_run)
        outputs.append(installer)
    return outputs


def is_elf(path):
    with path.open("rb") as source:
        return source.read(4) == b"\x7fELF"


def package_linux(args, destination, portable, base, version, env):
    outputs = []
    if not args.dry_run:
        validate_payload(destination, "linux")
    if args.format != "installer":
        archive = safe_output(base.with_name(base.name + "-portable.tar.gz"))
        reset_output(portable, args.dry_run)
        reset_output(archive, args.dry_run)
        root = portable / "DarkEye"
        if not args.dry_run:
            shutil.copytree(destination, root / f"app-{version}", symlinks=True)
            (root / "data").mkdir()
            (root / f"app-{version}/bin/portable.ini").write_text(
                "[Portable]\nDataDirectory=../../data\n", encoding="utf-8")
            launcher = root / "Darkeye.sh"
            launcher.write_text('#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                                f'exec "$HERE/app-{version}/bin/Darkeye" "$@"\n', encoding="utf-8")
            launcher.chmod(0o755)
            write_portable_readme(root, "linux")
            def archive_permissions(info):
                if info.name == "DarkEye/Darkeye.sh":
                    info.mode = 0o755
                return info
            with tarfile.open(archive, "w:gz") as package:
                package.add(root, arcname="DarkEye", filter=archive_permissions)
            with tarfile.open(archive, "r:gz") as package:
                if not package.getmembers():
                    raise RuntimeError("Empty portable archive")
        print(f"Portable archive: {archive}")
        outputs.append(archive)
    if args.format != "portable":
        dpkg = require_tool("dpkg-deb", args.dry_run)
        shlibdeps = require_tool("dpkg-shlibdeps", args.dry_run)
        installer = safe_output(base.with_name(base.name + "-setup.deb"))
        root = base.with_name(base.name + "-deb-root")
        reset_output(root, args.dry_run)
        reset_output(installer, args.dry_run)
        if not args.dry_run:
            shutil.copytree(destination, root / "opt/darkeye", symlinks=True)
            (root / "DEBIAN").mkdir()
            # Inspect all ELF code, including plugins, against host package metadata.
            elf_files = [p for p in destination.rglob("*") if p.is_file() and not p.is_symlink()
                         and is_elf(p)]
            control_source = root / "debian/control"
            control_source.parent.mkdir()
            control_source.write_text("Source: darkeye\nSection: utils\nPriority: optional\nMaintainer: Darkeye <support@darkeye.win>\nStandards-Version: 4.6.2\n\nPackage: darkeye\nArchitecture: amd64\nDescription: Darkeye desktop application\n")
            result = subprocess.run([shlibdeps, "--ignore-missing-info", "-O", f"-l{destination / 'lib'}",
                                     *[f"-e{p}" for p in elf_files]], cwd=root, env=env,
                                    capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f"Debian dependency analysis failed:\n{result.stderr}")
            dependencies = result.stdout.strip().removeprefix("shlibs:Depends=")
            if not dependencies or "shlibs:" in dependencies:
                raise RuntimeError(f"Cannot determine Debian runtime dependencies: {result.stdout}")
            dependencies += ", unzip, libsecret-1-0, libxcb-cursor0, fonts-noto-cjk"
            (root / "DEBIAN/control").write_text(
                f"Package: darkeye\nVersion: {version}\nArchitecture: amd64\n"
                "Maintainer: Darkeye <support@darkeye.win>\nSection: utils\nPriority: optional\n"
                f"Depends: {dependencies}\nInstalled-Size: {sum(p.stat().st_size for p in destination.rglob('*') if p.is_file()) // 1024}\n"
                "Description: Darkeye desktop application\n", encoding="utf-8")
            shutil.rmtree(control_source.parent)
            launcher = root / "usr/bin/darkeye"
            launcher.parent.mkdir(parents=True)
            launcher.write_text('#!/bin/sh\nexec /opt/darkeye/bin/Darkeye "$@"\n', encoding="utf-8")
            launcher.chmod(0o755)
            desktop = root / "usr/share/applications/darkeye.desktop"
            desktop.parent.mkdir(parents=True)
            desktop.write_text("[Desktop Entry]\nType=Application\nName=Darkeye\nExec=darkeye\nIcon=darkeye\nTerminal=false\nCategories=Utility;\n", encoding="utf-8")
            icon = root / "usr/share/icons/hicolor/scalable/apps/darkeye.svg"
            icon.parent.mkdir(parents=True)
            shutil.copy2(PROJECT_ROOT / "resources/icons/logo.svg", icon)
        run([dpkg, "--build", "--root-owner-group", str(root), str(installer)], env, args.dry_run)
        outputs.append(installer)
    return outputs


def write_update_package(destination, archive, system, arch, version, dry_run=False):
    """A common program-only tar.zst for both standard and portable installs."""
    archive = safe_output(archive)
    target = f"{system}-{arch}"
    manifest_path = safe_output(archive.with_name("latest.json"))
    if dry_run:
        print(f"Update package: {archive}")
        print(f"Update manifest: {manifest_path}")
        return archive
    payload = destination / "Darkeye.app" if system == "macos" else destination
    if not payload.is_dir() or payload.is_symlink():
        raise RuntimeError(f"Missing or linked update payload: {payload}")
    root = payload.resolve()

    def entries(directory):
        for path in sorted(directory.iterdir()):
            relative = path.relative_to(destination if system == "macos" else payload)
            if relative.parts[0] == "data" or path.name == "portable.ini":
                raise RuntimeError(f"User data in update payload: {path}")
            if path.is_symlink():
                target = Path(os.readlink(path))
                try:
                    resolved = path.resolve(strict=True)
                except (OSError, RuntimeError) as error:
                    raise RuntimeError(f"Broken or cyclic update link: {path}") from error
                if target.is_absolute() or not resolved.is_relative_to(root):
                    raise RuntimeError(f"External link in update payload: {path}")
                yield path, relative.as_posix()
            elif path.is_dir():
                yield path, relative.as_posix()
                yield from entries(path)
            elif path.is_file():
                yield path, relative.as_posix()
            else:
                raise RuntimeError(f"Unsupported file in update payload: {path}")

    archive.parent.mkdir(parents=True, exist_ok=True)
    # Preserve the original app layout, including relative framework links.
    with tempfile.TemporaryDirectory(prefix="update-", dir=archive.parent) as temporary:
        staged = Path(temporary) / archive.name
        try:
            import zstandard
        except ImportError as error:
            raise RuntimeError(
                "生成 .tar.zst 更新包需要 zstandard，请运行："
                "python -m pip install -r tools/requirements-packaging.txt") from error
        count = 0
        with staged.open("wb") as output:
            with zstandard.ZstdCompressor(level=10, write_checksum=True).stream_writer(output) as compressed:
                # PAX preserves long names, Unix permissions and link targets.
                with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT,
                                  dereference=False) as package:
                    if system == "macos":
                        package.add(payload, arcname="Darkeye.app", recursive=False)
                    for path, relative in entries(payload):
                        package.inodes.clear()  # Store hard-linked files as ordinary files.
                        info = package.gettarinfo(str(path), arcname=relative)
                        if info.isreg():
                            with path.open("rb") as data:
                                package.addfile(info, data)
                        else:
                            package.addfile(info)
                        count += 1
        if not count:
            raise RuntimeError("Empty update payload")
        # Read the complete stream back to verify the zstd checksum and TAR data.
        with staged.open("rb") as source:
            with zstandard.ZstdDecompressor().stream_reader(source) as decompressed:
                with tarfile.open(fileobj=decompressed, mode="r|") as package:
                    for member in package:
                        if member.isdir() or member.issym():
                            continue
                        if not member.isfile():
                            raise RuntimeError(f"Unsupported update archive entry: {member.name}")
                        with package.extractfile(member) as data:
                            for chunk in iter(lambda: data.read(1024 * 1024), b""):
                                pass
                while decompressed.read(1024 * 1024):
                    pass
        digest = file_sha256(staged)
        manifest = {"latestVersion": version, "releaseNotes": "",
                    "packages": {target: {
                        "url": f"https://darkeye.win/releases/{archive.name}",
                        "sha256": digest}}}
        staged_manifest = Path(temporary) / manifest_path.name
        staged_manifest.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        staged.replace(archive)
        staged_manifest.replace(manifest_path)
    print(f"Update package: {archive}")
    print(f"Update manifest: {manifest_path}")
    return archive


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksums(outputs, dry_run=False):
    for artifact in outputs:
        if not artifact.name.endswith("-update.tar.zst"):
            continue
        sidecar = safe_output(Path(str(artifact) + ".sha256"))
        if dry_run:
            print(f"SHA-256: {sidecar}")
            continue
        if not artifact.is_file():
            raise RuntimeError(f"Packaging did not produce {artifact}")
        sidecar.write_text(f"{file_sha256(artifact)}  {artifact.name}\n", encoding="utf-8")
        print(f"Artifact: {artifact}")


def package_project(args):
    system, arch = release_platform()
    if args.clean and args.skip_build:
        raise RuntimeError("--clean and --skip-build cannot be combined")
    if args.test and args.skip_build:
        raise RuntimeError("--test and --skip-build cannot be combined")
    if system == "windows":
        outputs = package_windows(args)
    else:
        version = project_version()
        base = safe_output((args.destination or PROJECT_ROOT / "out" / f"Darkeye-{version}-{system}-{arch}").absolute())
        destination = base.with_name(base.name + "-install")
        portable = base.with_name(base.name + "-portable")
        cmake, env, build_dir = build_project("Release", run_tests=args.test, dry_run=args.dry_run,
                                            clean=args.clean, skip_build=args.skip_build,
                                            build_directory=args.build_dir)
        reset_output(destination, args.dry_run)
        if not args.dry_run:
            destination.mkdir(parents=True)
        run([cmake, "--install", str(build_dir), "--config", "Release", "--prefix", str(destination)], env, args.dry_run)
        if system == "macos":
            outputs = package_macos(args, destination, portable, base, version, cmake, env)
        else:
            outputs = package_linux(args, destination, portable, base, version, env)
    version = project_version()
    base = safe_output((args.destination or PROJECT_ROOT / "out" / f"Darkeye-{version}-{system}-{arch}").absolute())
    destination = base.with_name(base.name + "-install")
    outputs.append(write_update_package(destination, base.with_name(base.name + "-update.tar.zst"),
                                        system, arch, version, args.dry_run))
    write_checksums(outputs, args.dry_run)
    # Cleanup only this release's known staging and obsolete artifacts, after
    # every requested artifact has been generated successfully.
    for artifact in outputs:
        if not args.dry_run and not artifact.is_file():
            raise RuntimeError(f"Packaging did not produce {artifact}")
    obsolete = [destination, base.with_name(base.name + "-portable"),
                base.with_name(base.name + "-deb-root"),
                destination.with_name(destination.name + "-components.plist"),
                base.parent / f"latest-{system}-{arch}.json",
                base.with_name(base.name + "-update.zip"),
                base.with_name(base.name + "-update.zip.sha256")]
    # Remove old distribution checksums, including formats no longer generated.
    for suffix in ("-portable.zip", "-portable.7z", "-portable.tar.gz",
                   "-setup.exe", "-setup.pkg", "-setup.dmg", "-setup.deb"):
        obsolete.append(base.with_name(base.name + suffix + ".sha256"))
    if system == "macos":
        obsolete.append(base.with_name(base.name + "-setup.pkg"))
    for path in obsolete:
        reset_output(path, args.dry_run)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print(f"构建、安装或压缩失败（退出码 {error.returncode}）。", file=sys.stderr)
        sys.exit(error.returncode if error.returncode > 0 else 1)
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
