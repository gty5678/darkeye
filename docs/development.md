# C++ / Qt 开发、构建与调试

本仓库使用 C++ / Qt、CMake、Ninja 和 vcpkg manifest 模式。本文主要介绍 Windows / MSVC 的本地开发流程；macOS 见 [macOS arm64 编译与打包](macos-packaging.md)，发布规则见[双平台构建与发布](cross-platform-packaging.md)。

## 前置条件

在 Windows 上准备以下工具：
- Git
- [Visual Studio 2022](https://aka.ms/vs/17/release/vs_community.exe)，
- CMake 3.25 或更高版本、Ninja。
- Qt 6.12.0 ，并且安装的 **MSVC 2022 64-bit** kit。
- vcpkg。项目的 C++ 第三方依赖由根目录的 `vcpkg.json` 自动解析，不要手动复制第三方库目录。

### 安装Git

### 安装 Visual Studio 2022
安装“使用 C++ 的桌面开发”工作负载（MSVC x64 工具链和 Windows SDK）。

### 安装 CMake 和 Ninja
这个一般随着vs C++桌面开发会同时安装上去

### 安装Qt

使用在线安装的方式，需要注册登陆账号，只安装Qt 6.12.0的MSVC依赖即可，大概5G不到

Windows 构建预设默认使用 `C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6`，不再读取可能指向旧版本的 `Qt6_DIR` 环境变量。使用其他安装位置时，在配置命令中覆盖路径：
```powershell
cmake --preset windows-msvc-release -DQt6_DIR=D:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6
```

非预设构建仍可设置以下环境变量供 `tools/build.py` 使用：
```powershell
$env:Qt6_DIR = "C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6"
```


### 安装vcpkg
实际上vcpkg会随着vs C++工作负载安装，但是最好自己装一个新的

若尚未安装 vcpkg，可执行：

```powershell
git clone https://github.com/microsoft/vcpkg C:/src/vcpkg
C:/src/vcpkg/bootstrap-vcpkg.bat
```

添加VCPKG_ROOT的环境变量，这个很重要
```
$env:VCPKG_ROOT = "C:/src/vcpkg"
```

#### vcpkg换源
首先先去安装[powershell 7.5.3](https://github.com/PowerShell/PowerShell/releases/download/v7.5.3/PowerShell-7.5.3-win-x64.msi) 这个后面编译的时候会用到，需要安装

Git URL重定向
```
git config --global url."https://gitee.com/mirrors/vcpkg".insteadOf "https://github.com/microsoft/vcpkg"
```

```
git config --global url."https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg/".insteadOf "https://github.com/microsoft/vcpkg"
```

设置环境变量
```
$env:VCPKG_DOWNLOAD_MIRROR="https://mirrors.ustc.edu.cn/github-release/ninja-build/ninja/"
$env:X_VCPKG_ASSET_SOURCES="x-azurl,https://mirrors.ustc.edu.cn/vcpkg/assets/"
```

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
python tools/package.py
```

此方式需要系统已有可用的 CMake、Ninja、Git 等工具；Windows 构建脚本会把 VS 自带工具加入环境。

构建目录位于 `build/windows-msvc-*`；不要把生成文件提交到 Git。

## 命令行构建与运行

安装 Python 3.9 或更高版本后，可使用统一构建入口（不需要额外 Python 包）：

```powershell
python tools/build.py                       # 默认 Release，不含 data
python tools/build.py --config Debug        # Debug
python tools/build.py --config Debug --test # 编译并运行测试
python tools/build.py --dry-run             # 查看将执行的命令
```

Windows 下脚本自动查找 Visual Studio、加载 x64 MSVC / Windows SDK 环境，
并使用上面的 CMake 预设。macOS 下使用 PATH 中的 CMake，构建目录为
`build/<系统>-<配置>`；需自行安装相应编译器、Qt 和依赖。macOS arm64 已完成本机编译与 ZIP/PKG 打包验证，依赖清单、环境配置、编译运行命令和本机快捷操作见 [macOS 安装依赖、编译、运行与打包](macos-packaging.md)。正式支持范围为 Windows x64 和 macOS ARM64，不提供 Linux 版本。

Windows 安装版和便携版统一通过打包脚本生成：

```powershell
python tools/package.py                       # 构建并生成安装 exe 和便携 7z
python tools/package.py --format portable     # 仅便携版
python tools/package.py --format installer    # 仅安装版
python tools/package.py --skip-build          # 使用现有 Release 构建重新打包
```

打包额外需要 7-Zip 和 Inno Setup（只生成一种时只需对应工具）。默认使用 `release` 增量构建，
输出 `out/Darkeye-<版本>-windows-x64-portable.7z` 和 `out/Darkeye-<版本>-windows-x64-setup.exe`。
全量重建使用 `--clean`。环境配置、完整操作步骤、目录布局和安装规则见
[Windows 安装版与便携版生成方法](windows-packaging.md#windows-packaging)。

在仓库根目录执行日常 Debug 构建：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset debug
```

生成的可执行文件为：

```text
build/windows-msvc-debug/apps/desktop/Darkeye.exe
```

构建完成后，CMake 会自动调用 Qt 的 `windeployqt`，因此可以直接运行该文件：

```powershell
& .\build\windows-msvc-debug\apps\desktop\Darkeye.exe
```

如需构建和运行测试，改用测试预设：

```powershell
cmake --preset windows-msvc-debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests
```

`ctest --preset debug-tests` 会在测试失败时输出失败信息。也可用 `ctest --test-dir build/windows-msvc-debug-tests --output-on-failure`
运行同一套测试。

## Visual Studio 2022 调试

1. 使用 **File → Open → Folder** 打开仓库根目录；Visual Studio 会读取 `CMakePresets.json`。
2. 在工具栏的配置下拉框选择 `windows-msvc-debug`；若要调试测试，选择 `windows-msvc-debug-tests`。
3. 在 CMake Targets View 中将 `Darkeye` 设为启动项，等待首次 CMake 配置和构建完成。
4. 在 C++ 源文件中设置断点，按 `F5` 开始调试；使用 `Ctrl+F5` 直接运行而不附加调试器。

普通 `Darkeye.exe` 调试目标传入 `--config-dir <项目根>/config --avwiki-dir <项目根>/avwiki --data-dir <项目根>/data`，直接使用开发目录，不在 exe 同级生成 data。新电脑检出项目后，Windows MSVC 首次 CMake 配置会自动从 `tools/launch.vs.json` 生成 `.vs/launch.vs.json`，无需手动复制；已有本地配置不会被覆盖。该配置只匹配普通目标；Install 目标不传路径参数，按安装目录中的资源与便携标记选择路径。使用 Visual Studio 工程生成器时，CMake 同样设置了构建目标的调试参数。

若 Visual Studio 提示找不到 Qt 或 vcpkg，请检查预设中的 `Qt6_DIR` 路径，并在启动 Visual Studio 前设置 `VCPKG_ROOT`，然后执行
**Project → Delete Cache and Reconfigure**。切换 Debug/Release 或切换依赖配置后，也应重新配置。

## VS Code 调试

安装 Microsoft 的 C/C++ 与 CMake Tools 扩展后，打开仓库根目录：

1. 在 CMake Tools 的配置预设中选择 `windows-msvc-debug`。
2. 执行 **CMake: Configure**，再执行 **CMake: Build**。
3. 选择 `Darkeye` 作为启动目标；在源文件添加断点后按 `F5`。

VS Code 必须从已设置 `VCPKG_ROOT` 的 Developer PowerShell 启动，或在系统环境变量中配置它；Qt 使用预设中的 6.12.0 kit 路径。

## 安装目录与发布构建

Release 构建不等同于安装包。构建完成后执行 `cmake --install`，才能得到可分发目录：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release
cmake --install build/windows-msvc-release --config Release
```

四个预设的安装目录分别为 `out/install/debug`、`out/install/debug-tests`、`out/install/release`、`out/install/release-tests`。Install 只部署程序、运行依赖以及必要的 `config/` 和 `avwiki/`，不复制开发数据，也不生成 `data`、`seed-data` 或 `portable.ini`。对外发布安装包或便携包使用 `tools/package.py`。

安装步骤会复制程序、只读 `config/`、初始 `avwiki/`、Qt 运行时和所需的 vcpkg DLL。交付前请在未安装开发环境的 Windows
机器上验证 `Darkeye.exe` 能否直接启动。

## 常见问题

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

