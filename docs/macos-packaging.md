# macOS arm64 安装依赖、编译、运行与打包

本文从一台 Apple Silicon Mac 的环境准备开始，说明如何编译 Darkeye、运行测试，并生成便携 ZIP 与安装 DMG。命令在终端中执行；进入源码目录后，后续命令均以仓库根目录为工作目录。

主程序及演示程序已在 Apple Silicon、Apple Clang 21、macOS SDK 27、Qt 6.12.0 和 CMake 3.30.5 下完成 Release 原生编译。仓库中的 vcpkg overlay 修复了固定 baseline 内 zlib/libpng 对现代 macOS SDK 的兼容问题；Windows 保留原有编译分支。最低系统版本和干净机器运行仍需进行本文末尾的验收。

`python3 tools/package.py` 的完整编译、部署、打包流程以及 `--skip-build --build-dir build/macos-arm64-release` 均已实测成功，生成便携 ZIP 和安装 DMG。验证包括 ZIP 完整性、便携标记与外部 data 目录、应用 ad-hoc 签名、DMG 内容清单；该验证针对旧版 PKG 流程，当前 DMG 流程需重新验收；未执行系统安装或 Developer ID 公证。运行前需按下文设置 Qt、vcpkg 和 CMake 3.x 环境。

该环境下全部测试目标可编译；原生 CTest 为 58/70 通过（含启动、UI 冒烟、3D/DVD 演示冒烟），12 项失败涉及数据库夹具、路径/界面断言及 GPU/DPR 检查，尚未全部通过。发布工具的 4 项测试通过。此次验证的依赖在本机默认系统部署目标下构建，因此不构成对 macOS 14.4 运行兼容性的验证。

## 0. 当前电脑直接使用

本节适用于已经完成依赖准备的这台电脑。新电脑请从第 1 节开始安装，不要直接复制本节的 vcpkg 路径。

构建脚本现在会自动识别默认的 `$HOME/Qt/6.12.0/macos` 和项目 `build/vcpkg`；如果 PATH 中选中 CMake 4，会优先使用 Qt 附带的 CMake 3.x。已有本机依赖时，无需设置环境变量即可执行 `python3 tools/package.py`。显式设置的 Qt/vcpkg/toolchain 路径仍优先使用。下面的环境变量命令也适用于直接运行 CMake。

每次新开终端，先执行：

```sh
cd /Users/yin/Desktop/darkeye
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"
export Qt6_DIR="$HOME/Qt/6.12.0/macos/lib/cmake/Qt6"
export VCPKG_ROOT="$PWD/build/vcpkg"
cmake --version
```

确认输出为 CMake 3.x。按需要选择下面的命令：

| 目的 | 命令 |
| --- | --- |
| 编译 Release | `python3 tools/build.py --config Release` |
| 运行编译后的应用 | `open build/macos-arm64-release/apps/desktop/Darkeye.app` |
| 编译并同时打包便携版、安装版 | `python3 tools/package.py` |
| 直接复用现有 Release 结果打包 | `python3 tools/package.py --skip-build --build-dir build/macos-arm64-release` |
| 只生成便携 ZIP | `python3 tools/package.py --format portable` |
| 只生成安装 DMG | `python3 tools/package.py --format installer` |

当前版本 1.2.5 的产物为 `out/Darkeye-1.2.5-macos-arm64-portable.zip` 和 `out/Darkeye-1.2.5-macos-arm64-setup.dmg`，同时生成 `.tar.zst` 更新包、该更新包的 `.sha256` 和 `latest.json`。便携版需完整解压后运行，见第 6 节。

这台电脑因 GitHub 连接问题使用了固定版本的 vcpkg 源码归档，在其 `installed/` 中安装依赖。脚本只有在识别到本机 `build/vcpkg` 无 Git 元数据、且 FreeType/msdfgen 已安装时，才自动设置 `VCPKG_MANIFEST_MODE=OFF`，因此重新配置也能复用依赖。正常 Git 版 vcpkg 保留 manifest 模式。新电脑仍应按第 3 节准备完整 Git 版 vcpkg。无需把这个本机设置提交进公共预设。

## 1. 确认系统和工具链

| 项目 | 要求 |
| --- | --- |
| CPU | Apple Silicon，终端和 Python 均原生运行于 arm64 |
| 应用最低运行系统 | 当前预设为 macOS 14.4 |
| 构建工具链 | Apple Clang 和 macOS SDK（本次使用 Command Line Tools）；也可使用完整 Xcode |
| Qt | 6.12.0 或更新的 Qt 6，macOS Desktop kit 必须含 arm64 |
| 构建工具 | CMake 3.25–3.x、Ninja、Python 3.9+、Git |
| 依赖管理 | vcpkg，目标 triplet 为 arm64-osx |
| 打包工具 | ditto、codesign、lipo、hdiutil，由系统及开发工具提供 |

先检查架构：

```sh
uname -m
python3 -c 'import platform; print(platform.machine())'
```

两条命令均应显示 `arm64`。不要使用 Rosetta 下的 x86_64 终端或 Python；本项目不生成 Intel 包。

先安装 Apple Command Line Tools，弹窗中完成安装后检查：

```sh
xcode-select --install
xcode-select -p
xcrun clang++ --version
xcrun --show-sdk-path
```

已经安装时，无需重复安装。此次原生编译使用 `/Library/Developer/CommandLineTools` 工具链。若选择安装完整 Xcode，完成首次启动初始化，并选择其工具链：

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
xcode-select -p
xcrun clang++ --version
```

如果 Xcode 安装在别处，替换第一条命令的路径。构建机系统要求与应用最低运行系统是两回事，部署目标 14.4 不代表所有版本的 Xcode 都能安装在 14.4 上。[Qt 支持平台](https://doc.qt.io/qt-6/supported-platforms.html)

已安装 Homebrew 时，可以用它准备通用工具：

```sh
brew install ninja python git pkg-config
ninja --version
python3 --version
git --version
pkg-config --version
```

Homebrew 安装与支持系统要求见[官方说明](https://docs.brew.sh/Installation)。若当前系统不在其支持范围内，可分别从 CMake、Python 等工具的官方渠道安装。无需通过 Homebrew 安装 Qt；以下步骤使用明确版本的 Qt kit。

另需安装 CMake 3.25–3.x：可在 Qt 安装器的 Tools 中选择 CMake（本次使用 3.30.5），或自行安装 CMake 3.x。当前 vcpkg baseline 包含旧版 Brotli，其 CMake 最低版本声明不兼容 CMake 4，因此不要直接使用 Homebrew 默认安装的 CMake 4。Qt 附带的 CMake 安装在默认位置时，先将它加入 PATH，再执行依赖安装和后续构建：

```sh
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"
cmake --version
```

## 2. 安装 Qt

通过 [Qt Online Installer](https://www.qt.io/download-qt-installer-oss) 安装 **Qt 6.12.0 的 macOS Desktop kit**，或更新的兼容 Qt 6 版本。应包含：

- Qt Core、Gui、Widgets、Network、Sql、Test 等基础模块。
- Qt Gui/Core 对应版本的私有开发头文件；项目使用 Qt 私有 API。
- Qt HttpServer、WebSockets、Svg、ShaderTools 模块及构建工具。

这些模块必须来自同一套 Qt 安装。当前项目不要求为了编译主程序而额外安装 Quick3D。若使用官方通用 macOS kit，库同时包含 Intel/arm64 切片也可以；本项目只编译 arm64 主程序。

假设安装目录为 `$HOME/Qt/6.12.0/macos`：

```sh
export Qt6_DIR="$HOME/Qt/6.12.0/macos/lib/cmake/Qt6"
test -f "$Qt6_DIR/Qt6Config.cmake"
```

最后一条命令应成功退出。安装位置可能不同，请根据实际存在的 `Qt6Config.cmake` 设置变量，不要照搬 Windows 的 Qt 路径。

## 3. 获取源码和配置 vcpkg

如果已有源码，进入现有仓库即可。新机器可以执行：

```sh
mkdir -p "$HOME/src"
cd "$HOME/src"
git clone https://github.com/gty5678/darkeye-cpp.git
cd darkeye-cpp
```

源码需要包含最新的 macOS 预设与打包脚本。已有仓库请确认自己正在使用包含这些改动的版本。

单独安装 vcpkg，不要复制 Windows 的 `vcpkg_installed`：

```sh
git clone https://github.com/microsoft/vcpkg.git "$HOME/src/vcpkg"
"$HOME/src/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/src/vcpkg"
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

首次配置时，CMake 根据 `vcpkg.json` 自动构建 FreeType、msdfgen 及其依赖，使用仓库固定的 baseline。无需手动执行 `vcpkg install`，也不需要用 vcpkg 再安装一套 Qt。首次构建需联网。[vcpkg 配置说明](https://learn.microsoft.com/vcpkg/get_started/get-started?pivots=shell-bash)

`export` 只影响当前终端。若需要每次打开终端自动生效，将以下三行加入自己的 `~/.zshrc`，路径按实际情况填写：

```sh
export VCPKG_ROOT="$HOME/src/vcpkg"
export Qt6_DIR="$HOME/Qt/6.12.0/macos/lib/cmake/Qt6"
export PATH="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"
```

## 4. 编译 Release 并运行测试

先确认位于包含 `CMakeLists.txt` 的项目根目录。仅编译：

```sh
python3 tools/build.py --config Release
```

脚本自动选择 `macos-arm64-release` 预设，使用 Ninja 调用 Apple Clang。构建目录为 `build/macos-arm64-release/`，主程序目标是 `Darkeye.app`。可查找构建出的应用：

```sh
find build/macos-arm64-release -name Darkeye.app -type d
```

构建目录中的应用用于开发，尚未经过完整发布依赖部署。分发与干净机器验证请使用打包产物。

从终端打开开发构建：

```sh
open build/macos-arm64-release/apps/desktop/Darkeye.app
```

希望在终端中看到输出，或指定独立的开发数据目录时，直接运行内部可执行文件：

```sh
./build/macos-arm64-release/apps/desktop/Darkeye.app/Contents/MacOS/Darkeye \
  --config-dir "$PWD/config" \
  --avwiki-dir "$PWD/avwiki" \
  --data-dir "$PWD/data"
```

该命令使用源码中的配置与知识库，并将数据库等用户数据写入项目 `data/`。不传参数时按应用正常路径规则选择资源和用户数据目录。Debug 构建使用 `python3 tools/build.py --config Debug`，运行路径相应替换为 `build/macos-arm64-debug/apps/desktop/Darkeye.app`。

编译测试并运行全部 CTest：

```sh
python3 tools/build.py --config Release --test
```

测试使用独立目录 `build/macos-arm64-release-tests/`。界面和 Metal 渲染测试需要正常图形登录会话，不能用纯 SSH/headless 环境的结果替代桌面验收。系统钥匙串测试默认会跳过需要显式启用的用例；在可使用测试钥匙串的桌面会话中，可设置 `DARKEYE_TEST_SYSTEM_KEYRING=1` 后单独验证。

查看失败测试详情：

```sh
ctest --test-dir build/macos-arm64-release-tests -C Release --output-on-failure
```

也可直接使用 CMake：

```sh
cmake --preset macos-arm64-release -DQt6_DIR="$Qt6_DIR"
cmake --build --preset macos-arm64-release
```

这里生成的是 Ninja 构建目录，不是 `.xcodeproj`；Xcode 提供编译工具链。更换 Qt 或编译器后可使用 `python3 tools/build.py --config Release --clean` 重建；此操作会清理该预设的构建目录，用户数据不应放在那里。

### 在 VS Code 中断点调试

不必安装完整 Xcode：本机已有的 Command Line Tools 提供 Apple Clang、SDK 和 LLDB。完整 Xcode 只在需要其 IDE 或额外工具时安装。当前 CMake 预设使用 Ninja，不生成 Xcode 工程。

VS Code 中安装 CodeLLDB（`vadimcn.vscode-lldb`）用于断点调试；可另外安装 CMake Tools（`ms-vscode.cmake-tools`）管理构建，以及 C/C++（`ms-vscode.cpptools`）提供代码导航。

本机工作区已准备 `.vscode/launch.json`、`tasks.json` 和 `extensions.json`。在 VS Code 中打开整个项目目录 `/Users/yin/Desktop/darkeye`，在 `apps/desktop/main.cpp` 的 `main()` 内设置断点，进入“运行和调试”，选择 **Darkeye macOS Debug**，按 F5。首次会自动配置、编译 Debug 主程序，后续为增量构建；然后启动 `.app/Contents/MacOS/Darkeye` 内部可执行文件。F10 单步跳过，F11 单步进入，Shift+F5 停止。

该本机配置使用 Qt 6.12.0、Qt 附带的 CMake 3.x，以及第 0 节的本机 vcpkg 已安装依赖（`VCPKG_MANIFEST_MODE=OFF`）。调试时显式使用项目 `config/`、`avwiki/` 和 `data/`。`.vscode/` 被 Git 忽略，不会改变 Windows 开发配置；新电脑需自行创建配置，并按实际 Qt、vcpkg 路径调整。

手动构建本机 Debug 版本可执行：

```sh
cmake --preset macos-arm64-debug \
  -DQt6_DIR="$Qt6_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_MODE=OFF
cmake --build --preset macos-arm64-debug --target Darkeye --parallel 8
```

上述 `OFF` 仅适用于第 0 节已安装好依赖的本机环境。新电脑按第 3 节使用完整 vcpkg Git 仓库后，可直接用 `python3 tools/build.py --config Debug`，保留 manifest 自动安装依赖。

调试对象应使用 Debug 构建；Release 优化可能导致变量无法查看、断点行跳动。无需先打包 ZIP/DMG 才能调试。若系统首次提示允许开发者工具调试，请按系统提示授权。

配置参考：[VS Code 的 macOS C++ 开发说明](https://code.visualstudio.com/docs/cpp/config-clang-mac)、[CodeLLDB 启动配置](https://github.com/vadimcn/codelldb/blob/master/MANUAL.md)。

## 5. 生成便携版和安装版

环境配置完成后，在项目根目录使用 `uv` 安装打包依赖（需已安装 `uv`）：

```sh
uv venv
uv pip install -r tools/requirements-packaging.txt
```

依赖只需首次打包或变更后安装，无需手动激活 `.venv`。然后构建并打包：

```sh
uv run --no-project python tools/package.py
```

该命令默认不运行测试。希望测试通过后才打包，执行：

```sh
uv run --no-project python tools/package.py --test
```

代码未变化、只希望重新打包现有 Release 构建时：

```sh
uv run --no-project python tools/package.py --skip-build --build-dir build/macos-arm64-release
```

如果第 4 节的 Release 测试构建已经通过，也可复用测试构建目录：

```sh
uv run --no-project python tools/package.py --skip-build --build-dir build/macos-arm64-release-tests
```

`--skip-build` 不会重新运行测试；不要用它把测试失败的构建标记为合格发布。它不能与 `--test` 或 `--clean` 同时使用。

默认输出：

```text
out/Darkeye-<版本>-macos-arm64-portable.zip
out/Darkeye-<版本>-macos-arm64-setup.dmg
out/Darkeye-<版本>-macos-arm64-update.tar.zst
out/Darkeye-<版本>-macos-arm64-update.tar.zst.sha256
out/latest.json
```

版本自动读取顶层 `CMakeLists.txt`。仅生成一种格式，或先预览命令：

```sh
uv run --no-project python tools/package.py --format portable
uv run --no-project python tools/package.py --format installer
uv run --no-project python tools/package.py --dry-run
```

重复执行会替换同名暂存目录和产物。若暂存的便携目录已经含有用户数据，脚本会拒绝删除；此时可通过 `--destination out/new-release/Darkeye-<版本>-macos-arm64` 使用新位置，`<版本>` 需替换为实际版本。

## 6. 使用与校验产物

验证校验和：

```sh
shasum -a 256 -c out/*-macos-arm64-*.sha256
```

便携 ZIP 解压后为：

```text
DarkEye/
  Darkeye.app/
  portable.ini
  data/
  README.txt
```

把整个 `DarkEye` 文件夹放在可写位置，再打开 `Darkeye.app`。`portable.ini` 的 `DataDirectory=data` 以标记文件所在目录为基准，因此数据在 `.app` 外。迁移时携带整个文件夹；升级时退出程序，替换 `.app` 并保留 `portable.ini`、`data/`。不要在已签名的 `.app` 内创建数据文件。

例如将 ZIP 解压到一个新目录后启动（版本号按实际产物修改）：

```sh
ditto -x -k out/Darkeye-1.2.5-macos-arm64-portable.zip out/portable-run
open out/portable-run/DarkEye/Darkeye.app
```

打开 `.dmg`，将 `Darkeye.app` 拖入 `Applications` 快捷方式，安装到 `/Applications/Darkeye.app`。它不带便携标记，使用 Qt 的系统用户数据目录；实际位置可在软件设置的运行路径中查看。初始知识库从 `.app/Contents/Resources/avwiki` 复制到数据目录后才编辑和更新，安装资源保持只读。

也可从终端打开安装向导，完成安装后启动应用：

```sh
open out/Darkeye-1.2.5-macos-arm64-setup.dmg
# 在安装向导中完成安装后再执行：
open /Applications/Darkeye.app
```

可检查签名和架构；下面路径需按实际暂存目录替换：

```sh
codesign --verify --deep --strict /Applications/Darkeye.app
lipo -archs /Applications/Darkeye.app/Contents/MacOS/Darkeye
```

架构应仅显示 `arm64`。这些检查不等于 Gatekeeper 公证通过。

## 7. 正式签名与公证

默认打包使用 **ad-hoc 签名**，方便开发验证，不等于 Developer ID 签名或公证。已在钥匙串中安装相应证书时，可执行：

```sh
uv run --no-project python tools/package.py \
  --mac-sign-identity "Developer ID Application: 你的开发者名称 (TEAMID)" \
  --mac-installer-identity "Developer ID Installer: 你的开发者名称 (TEAMID)"
```

脚本会先部署依赖，再签名内部代码和 `.app`，并使用 Installer 身份签名 DMG。**当前脚本不自动提交公证、不自动 stapling**；正式公开分发需完成 Apple 的公证流程，再重新生成最终分发文件的校验和。不要把本地签名校验成功当作已完成公证。[Apple 公证说明](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

## 8. 常见问题与验收

| 现象 | 处理 |
| --- | --- |
| 提示要求 arm64 host | 检查 `uname -m` 和 Python 架构，退出 Rosetta 终端 |
| 找不到 Ninja/CMake | 安装工具并确认它们位于 PATH |
| 找不到 Qt6/GuiPrivate/ShaderTools | 修正 Qt6_DIR，补齐同版本 kit 的模块和私有头文件 |
| vcpkg toolchain 不存在 | 检查 VCPKG_ROOT 指向 vcpkg 仓库根目录，执行 bootstrap |
| 更换 Qt 后链接或运行异常 | 用新的构建目录，或清理对应构建目录后重建 |
| 缺少 codesign/hdiutil 或 SDK | 检查 Xcode 安装、初始化和 xcode-select 指向 |
| 找不到签名身份 | 检查证书及私钥已导入当前用户钥匙串；本地验证可先用默认签名 |
| 无法打开、系统提示开发者未验证 | 检查分发签名和公证状态；ad-hoc 包不是正式公证包 |
| 数据目录不可写 | 移动整个便携目录到可写位置，检查 portable.ini 和数据目录权限 |
| 测试失败 | 保留 Testing/Temporary/LastTest.log 与具体断言，修复或明确诊断后再发布 |

在未安装 Qt、vcpkg 的另一台兼容 Mac 上分别验证 ZIP 和 DMG：启动、首次建库、知识库编辑、网络访问、凭据读写、图形渲染、中文路径、退出后重启。便携目录移动后应继续读取原数据；安装升级应保留用户数据库和知识库。系统钥匙串密码不会随便携目录自动迁移到另一台机器。

通用规则见[三平台构建与发布](cross-platform-packaging.md)，Linux 流程见[Linux 编译与打包](linux-packaging.md)。
