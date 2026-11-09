#!/usr/bin/env python3
"""Configure and build Darkeye with CMake (Python 3.9+)."""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parent.parent


def visual_studio_root(env):
    """Find an installation containing the x64 C++ toolchain."""
    vswhere = Path(env.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
        "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath", "-utf8"],
            check=True, capture_output=True, encoding="utf-8",
        )
        if result.stdout.strip():
            return Path(result.stdout.strip())
    fallback = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community")
    if fallback.is_dir():
        return fallback
    raise RuntimeError("找不到 Visual Studio C++ 工具链，请安装桌面 C++ 工作负载。")


def windows_environment(root, env):
    """Import the VS x64 environment into child processes only."""
    launcher = root / "Common7/Tools/Launch-VsDevShell.ps1"
    if not launcher.is_file():
        raise RuntimeError(f"找不到 Visual Studio 开发环境脚本：{launcher}")
    powershell = shutil.which("pwsh", path=env.get("PATH")) or shutil.which(
        "powershell", path=env.get("PATH")
    )
    if not powershell:
        raise RuntimeError("找不到 PowerShell，无法初始化 MSVC 环境。")
    child_env = env.copy()
    child_env["DARKEYE_BUILD_VS_ROOT"] = str(root)
    script = r"""
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
& (Join-Path $env:DARKEYE_BUILD_VS_ROOT 'Common7/Tools/Launch-VsDevShell.ps1') `
    -VsInstallationPath $env:DARKEYE_BUILD_VS_ROOT `
    -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$values = @{}
Get-ChildItem Env: | ForEach-Object { $values[$_.Name] = $_.Value }
Write-Output '__DARKEYE_ENV__'
$values | ConvertTo-Json -Compress
"""
    result = subprocess.run(
        [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
        env=child_env, capture_output=True, encoding="utf-8", errors="replace",
    )
    if result.returncode:
        raise RuntimeError(f"初始化 MSVC 环境失败：\n{result.stdout}{result.stderr}")
    marker = "__DARKEYE_ENV__"
    if marker not in result.stdout:
        raise RuntimeError("初始化 MSVC 环境未返回环境变量。")
    imported = json.loads(result.stdout.split(marker, 1)[1].strip())
    imported.pop("DARKEYE_BUILD_VS_ROOT", None)
    # VS may select its bundled vcpkg; keep the user's configured checkout.
    if env.get("VCPKG_ROOT"):
        imported["VCPKG_ROOT"] = env["VCPKG_ROOT"]
    return imported


def run(command, env, dry_run):
    print("+ " + subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=PROJECT_ROOT, env=env, check=True)


def clean_build_directory(build_dir, dry_run=False):
    """Remove only a direct build directory inside this project."""
    project = PROJECT_ROOT.resolve()
    build_root = project / "build"
    target = build_dir.resolve()
    if build_root.resolve() != build_root or target.parent != build_root:
        raise RuntimeError(f"拒绝清理项目 build 目录以外的路径：{target}")
    if build_dir.is_symlink() or getattr(build_dir, "is_junction", lambda: False)():
        raise RuntimeError(f"拒绝清理链接目录：{build_dir}")
    print(f"清理构建目录：{target}", flush=True)
    if not dry_run and target.exists():
        shutil.rmtree(target)


def macos_environment(env):
    """Discover the default Qt kit and optional project-local vcpkg checkout."""
    env = env.copy()
    local_vcpkg = PROJECT_ROOT / "build/vcpkg"
    if not env.get("VCPKG_ROOT") and (local_vcpkg / "scripts/buildsystems/vcpkg.cmake").is_file():
        env["VCPKG_ROOT"] = str(local_vcpkg)
    qt_dir = Path.home() / "Qt/6.12.0/macos/lib/cmake/Qt6"
    if not env.get("Qt6_DIR") and (qt_dir / "Qt6Config.cmake").is_file():
        env["Qt6_DIR"] = str(qt_dir)

    # The pinned dependency ports require CMake 3.x. Prefer the Qt-bundled
    # copy only when PATH does not already select a compatible CMake.
    cmake = shutil.which("cmake", path=env.get("PATH"))
    bundled_bin = Path.home() / "Qt/Tools/CMake/CMake.app/Contents/bin"
    if (bundled_bin / "cmake").is_file():
        version = subprocess.check_output([cmake, "--version"], text=True) if cmake else ""
        if not version.startswith("cmake version 3."):
            env["PATH"] = str(bundled_bin) + os.pathsep + env.get("PATH", "")

    # A source archive has no Git registry. Reuse its explicitly installed
    # dependencies, but never silently disable manifests for a normal checkout.
    if (not env.get("CMAKE_TOOLCHAIN_FILE")
            and Path(env.get("VCPKG_ROOT", "/")) == local_vcpkg
            and not (local_vcpkg / ".git").exists()):
        installed = local_vcpkg / "installed/arm64-osx/share"
        if all((installed / name).is_file() for name in (
                "msdfgen/msdfgenConfig.cmake", "freetype/freetype-config.cmake")):
            env.setdefault("VCPKG_MANIFEST_MODE", "OFF")
    return env


def build_project(configuration="Release", run_tests=False, dry_run=False, clean=False, skip_build=False, build_directory=None):
    env = os.environ.copy()
    system = platform.system().lower()
    config = configuration.lower()
    if system == "darwin":
        env = macos_environment(env)
        if not skip_build and not env.get("CMAKE_TOOLCHAIN_FILE"):
            toolchain = Path(env.get("VCPKG_ROOT", "/")) / "scripts/buildsystems/vcpkg.cmake"
            if not toolchain.is_file():
                raise RuntimeError("找不到 vcpkg：请设置 VCPKG_ROOT 为 vcpkg 根目录，或设置 CMAKE_TOOLCHAIN_FILE。详见 docs/macos-packaging.md。")

    if system == "windows":
        root = visual_studio_root(env)
        env.setdefault("VCPKG_ROOT", str(root / "VC/vcpkg"))
        if not dry_run:
            env = windows_environment(root, env)
        bundled_cmake = root / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        cmake = str(bundled_cmake) if bundled_cmake.is_file() else shutil.which(
            "cmake", path=env.get("PATH")
        )
        name = f"{config}-tests" if run_tests else config
        configure = [cmake, "--preset", f"windows-msvc-{name}"]
        build = [cmake, "--build", "--preset", name]
        build_dir = PROJECT_ROOT / "build" / (
            f"windows-msvc-{config}" + ("-tests" if run_tests else "")
        )
    else:
        cmake = shutil.which("cmake", path=env.get("PATH"))
        machine = platform.machine().lower()
        if system == "darwin":
            if machine not in ("arm64", "aarch64"):
                raise RuntimeError("macOS releases require an arm64 host and arm64 Qt kit.")
            prefix = "macos-arm64"
        else:
            raise RuntimeError(f"Unsupported build host: {system}/{machine}")
        name = f"{prefix}-{config}" + ("-tests" if run_tests else "")
        build_dir = PROJECT_ROOT / "build" / name
        configure = [cmake, "--preset", name]
        build = [cmake, "--build", "--preset", name]

    # Environment overrides keep committed presets independent of local Qt paths.
    for variable in ("Qt6_DIR", "CMAKE_TOOLCHAIN_FILE", "VCPKG_TARGET_TRIPLET", "VCPKG_MANIFEST_MODE"):
        if env.get(variable):
            configure.append(f"-D{variable}={env[variable]}")
    if build_directory is not None:
        build_dir = Path(build_directory).absolute()
        configure.extend(["-B", str(build_dir)])
        build = [cmake, "--build", str(build_dir), "--config", configuration, "--parallel"]

    if not cmake:
        raise RuntimeError("找不到 CMake，请安装 CMake 并加入 PATH。")
    if skip_build:
        if not dry_run:
            cache_file = build_dir / "CMakeCache.txt"
            if not cache_file.is_file():
                raise RuntimeError(f"No configured native build at {build_dir}")
            cache = cache_file.read_text(encoding="utf-8")
            for line in cache.splitlines():
                if line.startswith("CMAKE_BUILD_TYPE:"):
                    cached_configuration = line.partition("=")[2]
                    if cached_configuration and cached_configuration != configuration:
                        raise RuntimeError(f"Expected {configuration} build, found {cached_configuration}: {build_dir}")
        return cmake, env, build_dir
    if clean:
        clean_build_directory(build_dir, dry_run)
    run(configure, env, dry_run)
    run(build, env, dry_run)
    if run_tests:
        ctest = str(Path(cmake).with_name("ctest.exe" if system == "windows" else "ctest"))
        run([ctest, "--test-dir", str(build_dir), "-C", configuration,
             "--output-on-failure"], env, dry_run)
    return cmake, env, build_dir


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", "--configuration", type=str.capitalize,
                        choices=("Debug", "Release"), default="Release")
    parser.add_argument("--test", "--run-tests", action="store_true",
                        help="启用测试构建，编译成功后运行测试")
    parser.add_argument("--clean", action="store_true", help="Rebuild from scratch")
    parser.add_argument("--build-dir", type=Path, help="Override the preset build directory")
    parser.add_argument("--dry-run", action="store_true", help="只显示命令，不执行构建")
    args = parser.parse_args()
    build_project(args.config, args.test, args.dry_run, args.clean, build_directory=args.build_dir)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print(f"构建命令失败（退出码 {error.returncode}）。", file=sys.stderr)
        sys.exit(error.returncode if error.returncode > 0 else 1)
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
