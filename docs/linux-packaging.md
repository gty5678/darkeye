# Linux x86_64 编译与打包

本文以 **Ubuntu 24.04 x86_64** 为构建环境，说明如何从源码编译 Darkeye、运行测试，并生成便携 TAR.GZ 与安装 DEB。命令在 Bash 等终端中执行；获取源码后，后续命令均在项目根目录运行。

当前已提供 Linux 构建和打包实现，但尚未取得本项目在 Linux 上的完整原生验证结果。DEB 面向兼容的 Debian/Ubuntu 系统，不代表适配所有 Linux 发行版。

## 1. 确认系统和安装工具

| 项目 | 要求 |
| --- | --- |
| CPU | x86_64；当前不生成 Linux arm64 包 |
| 构建基线 | 本文采用 Ubuntu 24.04、GCC 13 |
| Qt | 6.12.0 或更新的 Qt 6，Linux Desktop x86_64 kit |
| 工具 | CMake 3.25+、Ninja、Python 3.9+、Git、pkg-config |
| 依赖管理 | vcpkg，目标 triplet 为 x64-linux |
| 安装包工具 | dpkg-deb、dpkg-shlibdeps，由 dpkg/dpkg-dev 提供 |
| 测试环境 | 图形桌面，或 Xvfb；图形渲染需要可工作的 OpenGL/Mesa 环境 |

检查系统：

```sh
uname -m
cat /etc/os-release
ldd --version
```

架构应为 `x86_64`。Qt 6.12 官方 Linux 安装器二进制以 Ubuntu 24.04 / glibc 2.39 为基线，因此不能保证生成的便携包在更老的发行版运行。若要支持更旧系统，需另行选择 Qt 源码构建方案并验证所有依赖，不能仅在新系统上修改标签。[Qt 支持平台](https://doc.qt.io/qt-6/supported-platforms.html)

在 Ubuntu 24.04 安装构建、打包与运行依赖：

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build python3 git curl zip unzip tar \
  pkg-config libsecret-1-dev dpkg-dev patchelf \
  libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev \
  libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
  libxcb-keysyms1 libxcb-render-util0 libxcb-xinerama0 libxcb-xkb1 \
  libfontconfig1 libdbus-1-3 xvfb xauth dbus-daemon fonts-noto-cjk
```

检查工具：

```sh
g++ --version
cmake --version
ninja --version
python3 --version
pkg-config --modversion libsecret-1
dpkg-deb --version
dpkg-shlibdeps --version
```

这里使用系统默认 GCC。若要改用 Clang，需要另行安装并在首次配置前设置 `CXX=clang++`，同时验证依赖和运行库兼容性。切换编译器时使用新构建目录，避免复用旧缓存。

## 2. 安装 Qt

使用 [Qt Online Installer](https://www.qt.io/download-qt-installer-oss) 安装 **Qt 6.12.0 的 Linux Desktop x86_64 kit**，或更新的兼容 Qt 6 版本。需包含：

- Core、Gui、Widgets、Network、Sql、Test 等基础模块。
- 与 Qt 库版本一致的 Core/Gui 私有开发头文件。
- HttpServer、WebSockets、Svg、ShaderTools 模块及构建工具。

不要仅依赖发行版默认 `qt6-base-dev`：其版本可能低于项目要求，而且私有头文件、ShaderTools 等模块未必齐全。不要混用系统 Qt 和安装器 Qt 的不同版本。Qt 的 xcb 插件运行依赖见[官方 Linux 要求](https://doc.qt.io/qt-6/linux-requirements.html)。

假设 Qt 安装目录为 `$HOME/Qt/6.12.0/gcc_64`：

```sh
export Qt6_DIR="$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6"
test -f "$Qt6_DIR/Qt6Config.cmake"
```

若安装器使用不同的 kit 目录名，以实际的 `Qt6Config.cmake` 位置为准。

## 3. 获取源码和配置 vcpkg

已有源码时，进入现有仓库即可。新机器可以执行：

```sh
mkdir -p "$HOME/src"
cd "$HOME/src"
git clone https://github.com/gty5678/darkeye-cpp.git
cd darkeye-cpp
```

源码需包含最新的 Linux 构建预设和打包脚本。不要把 Windows 的 `build/`、`out/`、`.venv/` 或 `vcpkg_installed/` 复制过来复用。

安装 vcpkg：

```sh
git clone https://github.com/microsoft/vcpkg.git "$HOME/src/vcpkg"
"$HOME/src/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/src/vcpkg"
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

CMake 会根据仓库 `vcpkg.json` 的固定 baseline 自动构建 FreeType、msdfgen 及其依赖，不需要额外手动执行 `vcpkg install`。Qt 使用上一节安装的 kit。首次配置需要联网下载工具和依赖。[vcpkg 配置说明](https://learn.microsoft.com/vcpkg/get_started/get-started?pivots=shell-bash)

`export` 仅对当前终端生效。需要持久生效时，将以下内容加入自己的 `~/.bashrc`（其他 shell 使用对应配置文件）：

```sh
export VCPKG_ROOT="$HOME/src/vcpkg"
export Qt6_DIR="$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6"
```

## 4. 编译 Release

确认位于包含 `CMakeLists.txt` 的项目根目录，执行：

```sh
python3 tools/build.py --config Release
```

脚本识别 Linux x86_64，选择 `linux-x64-release` 预设，通过 Ninja 调用系统 C++ 编译器。构建输出在 `build/linux-x64-release/`，可查找主程序：

```sh
find build/linux-x64-release -type f -name Darkeye
```

开发构建目录依赖本机开发环境；在另一台机器上运行时请使用完整打包产物。

直接使用 CMake 的等价入口：

```sh
cmake --preset linux-x64-release -DQt6_DIR="$Qt6_DIR"
cmake --build --preset linux-x64-release
```

更换 Qt 或编译器后可使用新构建目录，或执行 `python3 tools/build.py --config Release --clean`。清理会删除该预设的构建目录，不应在构建目录保存用户数据。

## 5. 运行测试

在桌面或 CI 中，可以统一使用独立的 D-Bus 会话和 Xvfb：

```sh
dbus-run-session -- xvfb-run -a python3 tools/build.py --config Release --test
```

测试构建使用 `build/linux-x64-release-tests/`。测试失败时脚本返回非零状态；查看详情：

```sh
dbus-run-session -- xvfb-run -a \
  ctest --test-dir build/linux-x64-release-tests -C Release --output-on-failure
```

已有可用桌面会话时，也可以直接运行 `python3 tools/build.py --config Release --test`。测试配置按用例分别选择 offscreen 或 xcb；不要将所有测试强制设为 offscreen，否则不能验证原生界面和 GPU 功能。Xvfb 提供显示服务，不保证所有图形驱动和渲染断言一定通过。

凭据保存依赖 **Secret Service**，例如桌面会话中的 GNOME Keyring。只有 D-Bus 不代表已有可用的密码服务。系统钥匙串相关测试在非 Windows 默认跳过需要显式启用的用例；准备好测试 keyring 后可设置 `DARKEYE_TEST_SYSTEM_KEYRING=1` 再验证。

## 6. 生成便携版和安装版

一条命令编译并生成两种格式：

```sh
python3 tools/package.py
```

默认不会运行测试。希望测试通过后才打包，执行：

```sh
dbus-run-session -- xvfb-run -a python3 tools/package.py --test
```

已完成第 5 节测试时，可以复用其 Release 构建：

```sh
python3 tools/package.py --skip-build --build-dir build/linux-x64-release-tests
```

`--skip-build` 不会重新编译或测试；不能与 `--test`、`--clean` 同时使用。

默认输出：

```text
out/Darkeye-<版本>-linux-x64-portable.tar.gz
out/Darkeye-<版本>-linux-x64-portable.tar.gz.sha256
out/Darkeye-<版本>-linux-x64-setup.deb
out/Darkeye-<版本>-linux-x64-setup.deb.sha256
```

版本取自顶层 CMake 配置。便携包是携带私有运行库的目录归档，不是 AppImage；安装包是 DEB，当前不生成 RPM。

其他入口：

```sh
python3 tools/package.py --format portable
python3 tools/package.py --format installer
python3 tools/package.py --dry-run
```

默认重复执行会替换同名暂存目录和产物，但会拒绝清理包含用户数据的便携目录。可用 `--destination out/new-release/Darkeye-<版本>-linux-x64` 切换到新的项目内输出位置，`<版本>` 替换为实际版本。不要用 `sudo` 运行构建或打包脚本。

DEB 打包会使用 `dpkg-shlibdeps` 分析主程序和插件等 ELF 文件的系统依赖，并补充 unzip、libsecret、中文字体等依赖。如果分析失败，应解决缺少的库或包元数据，不要直接删除依赖声明继续发布。

## 7. 校验和使用产物

校验文件完整性：

```sh
sha256sum -c out/*-linux-x64-*.sha256
```

便携包解压后为以下结构，其中 `<版本>` 随构建变化：

```text
DarkEye/
  Darkeye.sh
  app-<版本>/
    bin/Darkeye
    bin/portable.ini
    lib/
    plugins/
    share/darkeye/
  data/
```

以下以版本 `1.2.5` 为例；构建其他版本时修改 `VERSION`。在希望保存便携数据的可写目录中解压，`tar -xzf` 后填写归档的实际路径：

```sh
VERSION=1.2.5
mkdir -p "$HOME/Applications/Darkeye-portable"
tar -xzf "out/Darkeye-${VERSION}-linux-x64-portable.tar.gz" -C "$HOME/Applications/Darkeye-portable"
cd "$HOME/Applications/Darkeye-portable/DarkEye"
./Darkeye.sh
```

以上命令应从源码根目录开始执行，最后会进入便携目录。整个目录必须位于可写文件系统，程序所在挂载点也不能禁止执行。`portable.ini` 中的 `DataDirectory=../../data` 相对于 `app-<版本>/bin/`，最终指向 `DarkEye/data/`。

迁移时移动整个 `DarkEye`。升级时退出程序，保留 `data/`，替换程序目录及与新版本对应的 `Darkeye.sh`。不要把个人数据库放进可被替换的 `app-<版本>/`。

安装 DEB 时先回到源码根目录，再执行（示例版本按实际修改）：

```sh
VERSION=1.2.5
sudo apt install "./out/Darkeye-${VERSION}-linux-x64-setup.deb"
```

应用安装到 `/opt/darkeye`，注册 `darkeye` 命令和桌面入口，之后可运行：

```sh
darkeye
```

安装模式使用 Qt 的系统用户数据目录，而不是 `/opt/darkeye`；通常在用户的 XDG 数据目录下，具体位置以软件设置显示的运行路径为准。知识库首次复制到用户数据目录的 `avwiki/`，之后编辑和下载更新不写入系统安装目录。

查看包内容和声明的依赖：

```sh
VERSION=1.2.5
dpkg-deb --contents "./out/Darkeye-${VERSION}-linux-x64-setup.deb"
dpkg-deb --info "./out/Darkeye-${VERSION}-linux-x64-setup.deb"
```

上述命令在源码根目录运行，`VERSION` 修改为实际版本。安装升级使用新 DEB 覆盖应用文件；移除 `darkeye` 包不会主动删除用户数据。便携目录中的密码凭据仍依赖系统 Secret Service，不能仅靠移动 `data/` 迁移密码。

## 8. 常见问题与验收

| 现象 | 处理 |
| --- | --- |
| GLIBC_2.39 或 GLIBCXX 符号找不到 | 目标系统运行库比构建基线旧；使用兼容系统，或另行制定旧系统构建方案 |
| 找不到 Qt6/GuiPrivate/ShaderTools | 修正 Qt6_DIR，安装同 kit 的所需模块及私有头文件 |
| 找不到 vcpkg toolchain | 检查 VCPKG_ROOT 是仓库根目录，确认 bootstrap 完成 |
| 找不到 libsecret-1 | 安装 libsecret-1-dev 与 pkg-config |
| 找不到 xcb 平台插件或插件加载失败 | 设置 QT_DEBUG_PLUGINS=1 获取诊断，检查 xcb 依赖；不要复制另一版本 Qt 的插件 |
| Could not connect to display | 在正常桌面会话中启动；自动测试使用 xvfb-run |
| OpenGL/QRhi 初始化失败 | 检查显卡驱动、Mesa 和显示服务，Xvfb 不会自动修复驱动问题 |
| WebDAV 密码无法保存 | 确认已运行且已解锁 Secret Service；仅安装 libsecret 并不提供密码服务 |
| 知识库 ZIP 无法解压 | 安装 unzip；GNU tar 不能替代此处的 ZIP 解压工具 |
| Permission denied | 检查文件执行权限、目录写权限和挂载点 noexec 设置 |
| 找不到 dpkg-shlibdeps | 安装 dpkg-dev；只打便携包可选择 --format portable |
| 数据清理被拒绝 | 保留已有 data，改用新的 --destination 暂存位置 |

发布前在没有 Qt/vcpkg 开发环境的兼容 Linux 桌面上分别验证便携包和 DEB：首次启动与建库、知识库编辑、中文字体和路径、图形渲染、网络访问、凭据保存、重启恢复。便携目录移动后应保留数据；安装升级应保留数据库。Wayland 和 X11 应按实际声明的支持范围分别验收，不要仅凭 Xvfb 通过就声称两者都已验证。

通用规则见[三平台构建与发布](cross-platform-packaging.md)，Apple Silicon 流程见[macOS 编译与打包](macos-packaging.md)。
