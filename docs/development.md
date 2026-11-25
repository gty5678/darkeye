# C++ / Qt 开发、构建与调试

本仓库使用 C++ / Qt、CMake、Ninja 和 vcpkg manifest 模式。本文主要介绍 Windows / MSVC 的本地开发流程；macOS 见 [macOS arm64 编译与打包](macos-packaging.md)，发布规则见[双平台构建与发布](cross-platform-packaging.md)。

## Windows x64 开发环境

### 1. 安装 Git、VS Code 和 uv

在 PowerShell 中执行以下命令。有已安装的工具时，可跳过对应命令。
也可以使用官方安装说明。[Git for Windows](https://git-scm.com/install/windows)、[VS Code](https://code.visualstudio.com/docs/setup/windows)、[uv](https://docs.astral.sh/uv/getting-started/installation/)

```powershell
winget install --id Git.Git --exact --source winget
winget install --id Microsoft.VisualStudioCode --exact --source winget
winget install --id astral-sh.uv --exact --source winget
```

安装后重新打开 PowerShell，让终端读取更新后的 `PATH`。项目构建脚本需要 Python 3.9 或更高版本。
已有合适版本时，uv 可以直接使用。否则可执行 `uv python install 3.12` 安装 Python。

```powershell
git --version
uv --version
uv run python --version
code --install-extension ms-vscode.cpptools
```

构建脚本优先使用 PowerShell 7（`pwsh`），也支持 Windows 自带的 Windows PowerShell。
如需安装 PowerShell 7，执行 `winget install --id Microsoft.PowerShell --exact --source winget`。[PowerShell 安装说明](https://learn.microsoft.com/zh-cn/powershell/scripting/install/install-powershell-on-windows)

### 2. 安装 MSVC、Windows SDK、CMake 和 Ninja

下载并运行 Visual Studio Build Tools 2022 安装器。已经安装 Visual Studio 2022 的读者，可以打开 **Visual Studio Installer**，对现有实例选择 **修改**。
日常编辑和调试使用 VS Code，编译使用这里安装的工具链。[下载 Build Tools 2022](https://aka.ms/vs/17/release/vs_buildtools.exe)、[VS 2022 安装组件说明](https://learn.microsoft.com/zh-cn/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022)

1. 在 **工作负载** 中勾选 **使用 C++ 的桌面开发**。
2. 在 **安装详细信息** 或 **单个组件** 中确认已选中以下组件。

    - **MSVC v143 - VS 2022 C++ x64/x86 生成工具（最新）**
    - **Windows 11 SDK**，也可使用已安装的 Windows 10 SDK
    - **适用于 Windows 的 C++ CMake 工具**，随组件安装 CMake 和 Ninja
    - **vcpkg 包管理器**

3. 完成安装后，从开始菜单打开 **Developer PowerShell for VS 2022**。如果快捷方式启动的是 x86 环境，执行以下命令切换到 x64，然后检查工具。[开发者终端说明](https://learn.microsoft.com/zh-cn/visualstudio/ide/reference/command-prompt-powershell?view=vs-2022)

```powershell
Enter-VsDevShell -VsInstallPath $env:VSINSTALLDIR -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VSCMD_ARG_TGT_ARCH   # 应显示 x64
Get-Command cl.exe, rc.exe
cmake --version          # 3.25 或更高版本
ninja --version
```

如果缺少 CMake 或 Ninja，先在 Installer 中确认 CMake 工具组件已安装。如需单独安装 CMake，
可下载 3.25 或更高版本的 3.x Windows x64 MSI，安装时勾选添加到 `PATH`。Ninja 可执行
`winget install --id Ninja-build.Ninja --exact --source winget` 安装。重新打开终端后检查版本。[CMake 下载](https://cmake.org/files/)、[Ninja 下载](https://github.com/ninja-build/ninja/releases)

### 3. 安装 Qt 6.12.0

1. 运行 Windows x64 版 Qt Online Installer，登录 Qt Account 后选择 **自定义安装**，安装目录填写 `C:\Qt`。[Qt 在线安装说明](https://doc.qt.io/qt-6/qt-online-installation.html)
2. 展开 **Qt 6.12.0**，勾选 **MSVC 2022 64-bit**，并在附加库中选择 **Qt HTTP Server**、**Qt WebSockets** 和 **Qt Shader Tools**。
   已有 Qt 安装时，可以通过 Qt Maintenance Tool 添加这些组件。
3. 确认 `C:\Qt\6.12.0\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake` 存在。

Windows 预设默认使用上述路径。若将 Qt 安装到其他位置，为 `tools/build.py` 和 VS Code 的启动任务设置用户环境变量 `Qt6_DIR`，值为包含 `Qt6Config.cmake` 的目录。
完全退出并重新打开 VS Code 后，启动任务会读取该变量。手动执行 CMake 预设时，通过 `-DQt6_DIR` 传入相同目录：

```powershell
cmake --preset windows-msvc-debug -DQt6_DIR="D:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6"
```

### 4. 配置 vcpkg

安装了 **vcpkg 包管理器** 组件后，`tools/build.py` 和 VS Code 启动任务会默认使用 Visual Studio 安装目录下的 `VC\vcpkg`。
项目通过仓库根目录的 `vcpkg.json` 声明依赖，首次 CMake 配置时会自动下载和构建。[vcpkg manifest 模式](https://learn.microsoft.com/zh-cn/vcpkg/consume/manifest-mode)

如需单独安装或管理 vcpkg，可在普通 PowerShell 中执行：

```powershell
git clone https://github.com/microsoft/vcpkg.git C:/src/vcpkg
& 'C:\src\vcpkg\bootstrap-vcpkg.bat'
$env:VCPKG_ROOT = 'C:\src\vcpkg'
& "$env:VCPKG_ROOT\vcpkg.exe" version
```

`$env:VCPKG_ROOT` 对当前终端生效。要让从开始菜单打开的 VS Code 也使用独立安装的 vcpkg，在 Windows 的 **编辑帐户的环境变量** 中新增用户变量 `VCPKG_ROOT`，值填 `C:\src\vcpkg`，再重启 VS Code 和终端。
手动使用 CMake 时，在开发者终端检查 `Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`。如果使用 Visual Studio 自带的 vcpkg，而变量尚未设置，可执行 `$env:VCPKG_ROOT = Join-Path $env:VSINSTALLDIR 'VC/vcpkg'`。

如果 vcpkg 下载源码或工具较慢，可以在当前终端通过 `X_VCPKG_ASSET_SOURCES` 配置可访问的资产缓存。[资产缓存配置](https://learn.microsoft.com/zh-cn/vcpkg/users/assetcaching)

## CMake 预设

根目录的 `CMakePresets.json` 已定义下列预设：

| 用途 | 配置预设 | 构建预设 |
| --- | --- | --- |
| 日常 Debug 构建 | `windows-msvc-debug` | `debug` |
| Debug 构建并启用测试 | `windows-msvc-debug-tests` | `debug-tests` |
| Release 构建 | `windows-msvc-release` | `release` |
| Release 构建并启用测试 | `windows-msvc-release-tests` | `release-tests` |

首次配置时，vcpkg 会下载并构建 `vcpkg.json` 中锁定的依赖，耗时会比后续配置更长。
如果 vcpkg 使用浅克隆，缺少锁定基线时先补取该提交（无需切换分支）：

```powershell
$VcpkgBaseline = (Get-Content vcpkg.json -Raw | ConvertFrom-Json).'builtin-baseline'
git -C $env:VCPKG_ROOT fetch --depth=1 origin $VcpkgBaseline
```

若 vcpkg 的构建工具下载失败，可在当前终端临时使用已经安装的系统工具：

```powershell
$env:VCPKG_FORCE_SYSTEM_BINARIES = "1"
uv run python tools/build.py --config Debug --target Darkeye
```

此方式需要系统已有可用的 CMake、Ninja、Git 等工具；Windows 构建脚本会把 VS 自带工具加入环境。

构建目录位于 `build/windows-msvc-*`；不要把生成文件提交到 Git。

## 命令行构建与运行

在仓库根目录打开 PowerShell，执行与 VS Code 调试前任务相同的构建命令。首次配置会下载依赖，后续构建会复用已有产物。

```powershell
uv run python tools/build.py --config Debug --target Darkeye
.\tools\run.ps1 -Configuration Debug
```

Debug 可执行文件位于 `build/windows-msvc-debug/Darkeye.exe`，Release 位于 `build/windows-msvc-release/Darkeye.exe`。
构建时 CMake 会调用 `windeployqt`，运行脚本会把仓库的 `config/`、`avwiki/` 和 `data/` 传给程序。
已有 Python 3.9 或更高版本并且 `python` 在 `PATH` 中时，也可以直接执行 `python tools/build.py`。

```powershell
uv run python tools/build.py --config Release --target Darkeye
uv run python tools/build.py --config Debug --test
uv run python tools/build.py --config Debug --dry-run
```

Windows 下脚本自动查找 Visual Studio、加载 x64 MSVC / Windows SDK 环境，
并使用上面的 CMake 预设。macOS 下使用 PATH 中的 CMake，构建目录为
`build/<系统>-<配置>`；需自行安装相应编译器、Qt 和依赖。macOS arm64 已完成本机编译与 ZIP/PKG 打包验证，依赖清单、环境配置、编译运行命令和本机快捷操作见 [macOS 安装依赖、编译、运行与打包](macos-packaging.md)。正式支持范围为 Windows x64 和 macOS ARM64，不提供 Linux 版本。

Windows 安装版和便携版统一通过打包脚本生成：

```powershell
uv run python tools/package.py                     # 构建并生成安装 exe 和便携 7z
uv run python tools/package.py --format portable   # 仅便携版
uv run python tools/package.py --format installer  # 仅安装版
uv run python tools/package.py --skip-build        # 使用现有 Release 构建重新打包
```

打包额外需要 7-Zip 和 Inno Setup（只生成一种时只需对应工具）。默认使用 `release` 增量构建，
输出 `out/Darkeye-<版本>-windows-x64-portable.7z` 和 `out/Darkeye-<版本>-windows-x64-setup.exe`。
全量重建使用 `--clean`。环境配置、完整操作步骤、目录布局和安装规则见
[Windows 安装版与便携版生成方法](windows-packaging.md#windows-packaging)。

如需绕过构建脚本直接调用 CMake，在 x64 Developer PowerShell 中确认 `VCPKG_ROOT` 后，从仓库根目录执行：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset debug --target Darkeye
& .\tools\run.ps1 -Configuration Debug
```

如需构建和运行测试，改用测试预设：

```powershell
cmake --preset windows-msvc-debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests
```

`ctest --preset debug-tests` 会在测试失败时输出失败信息。也可用 `ctest --test-dir build/windows-msvc-debug-tests --output-on-failure`
运行同一套测试。

## VS Code 调试

用 VS Code 打开包含 `CMakePresets.json` 和 `.vscode/` 的仓库根目录。确认已安装 Microsoft 的 C/C++ 扩展（`ms-vscode.cpptools`），并可在 VS Code 的终端中执行 `uv --version`。
在 **运行和调试** 下拉框选择 **Darkeye MSVC Debug** 或 **Darkeye MSVC Release**，按 `F5` 开始调试，或按 `Ctrl+F5` 直接运行。
启动任务会初始化 x64 MSVC 环境、配置对应预设并增量构建 Darkeye。构建脚本默认使用 Visual Studio 自带的 vcpkg，也支持通过 `VCPKG_ROOT` 指定安装位置。程序通过启动参数使用项目根目录的 `config/`、`avwiki/` 和 `data/`。
Release 保留编译优化并生成 PDB，部分变量可能被优化掉，单步顺序也可能与源码不同。

## 安装目录与发布构建

在 Visual Studio 的 x64 Developer PowerShell 中执行以下命令。Qt 部署工具使用该环境查找 Windows SDK 中的 Direct3D 组件和 MSVC 运行库。

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release
cmake --install build/windows-msvc-release --config Release
```

四个预设的安装目录分别为 `out/install/debug`、`out/install/debug-tests`、`out/install/release`、`out/install/release-tests`。Install 只部署程序、运行依赖以及必要的 `config/` 和 `avwiki/`，不复制开发数据，也不生成 `data`、`seed-data` 或 `portable.ini`。对外发布安装包或便携包使用 `tools/package.py`。

安装步骤会复制程序、只读 `config/`、初始 `avwiki/`、Qt 运行时和所需的 vcpkg DLL。交付前请在未安装开发环境的 Windows
机器上验证 `Darkeye.exe` 能否直接启动。

## 常见问题

### 找不到 cl.exe、CMake 或 Ninja

手动执行 CMake 命令时，请使用 x64 Developer PowerShell。若该终端仍找不到工具，回到 Visual Studio Installer 检查 MSVC x64/x86 和 C++ CMake 工具组件。若 VS Code 启动任务报找不到 `uv`，重新打开 VS Code 并运行 `Get-Command uv` 检查路径。

### CMake 找不到 Qt6

确认 `Qt6_DIR` 指向类似
`C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6` 的目录，而不是 Qt 安装根目录；然后删除 CMake 缓存并重新配置。

### vcpkg 工具链或依赖找不到

确认 `VCPKG_ROOT` 是 vcpkg 根目录，其中包含 `scripts/buildsystems/vcpkg.cmake`。不要用全局安装的包替代 manifest
依赖；`vcpkg.json` 的 baseline 应随依赖更新一并提交。

### 程序启动时缺少 Qt DLL 或插件

先重新执行对应的 `cmake --build --preset ...`。默认构建会在可执行文件旁运行 `windeployqt`；如果关闭了
`DARKEYE_DEPLOY_QT_RUNTIME`，需自行部署 Qt runtime，或改用 `cmake --install` 生成安装目录。

# webdav测试

```
winget install Rclone.Rclone
```

```
mkdir D:\webdav-test
```

```
rclone serve webdav D:\webdav-test --addr 127.0.0.1:8090 --user test --pass 123456
```

