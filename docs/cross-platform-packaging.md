# 双平台构建与发布

各平台从环境准备到产物验收的独立操作文档：

- [Windows x64](windows-packaging.md)
- [macOS arm64](macos-packaging.md)

## 支持范围

正式支持 Windows x64 和 macOS ARM64；不提供 Linux 构建、安装包或自动更新。

- Qt **6.12.0 或更新的 Qt 6**；C++20、CMake 3.25+、Ninja、Python 3.9+。
- Windows x64：MSVC 2022；便携 `.7z` 和 Inno Setup `.exe`。
- macOS **arm64**：Xcode 16+，macOS 14.4+；便携 `.zip` 和安装 `.dmg`。不生成 Intel 版本。

Qt 的最低运行系统随实际使用的 Qt kit 确定。使用更新版本时需重新核对其系统要求。
本项目使用 Qt 私有 API，因此每个发布包必须携带构建时对应的 Qt 运行库，不可混装其他 Qt 版本。

参考：[Qt 支持平台](https://doc.qt.io/qt-6/supported-platforms.html)、[Qt CMake 部署](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)。

## 目录与数据

| 内容 | Windows | macOS |
| --- | --- | --- |
| 程序 | 安装目录/Darkeye.exe | Darkeye.app/Contents/MacOS/Darkeye |
| 只读配置、知识库种子、许可证 | 程序目录 | .app/Contents/Resources |
| 标准模式数据 | Qt AppLocalDataLocation | Qt AppLocalDataLocation |
| 便携标记 | app-版本/portable.ini | .app 外同级 portable.ini |
| 便携数据 | DarkEye/data | DarkEye/data |

知识库首次运行从安装资源复制到数据目录的 `avwiki/`，随后编辑和下载更新均写入此副本。已有副本不会被启动或升级覆盖，用户删除的页面也不会在下次启动重新出现。原安装目录中的知识库保留不变。

`--config-dir`、`--avwiki-dir`、`--data-dir` 仍可显式覆盖默认目录；`--avwiki-dir` 表示直接使用调用者指定的工作知识库，不自动初始化。
数据目录优先级：命令行 > `DARKEYE_DATA_DIR` > `portable.ini` > Qt 系统用户目录。便携配置损坏或数据不可写时报告错误，不静默切换数据库。

知识库 ZIP 更新使用系统 `tar`，缺少工具会明确报错。

便携升级替换程序部分，保留 `data/`。macOS 用户应先把整个文件夹移到可写位置再打开 `.app`，不在只读镜像中运行。系统凭据存储中的密码仍绑定系统账号，换机器需重新输入；数据库中的外部媒体绝对路径也不会自动转换为另一个系统的路径。


双平台的每周自动检查和设置页手动检查均使用独立更新器。用户确认后退出主程序，由更新器下载、校验 SHA-256、覆盖程序文件并重启；失败时回滚。不兼容旧的 `DarkEyeUpdater.exe` 和顶层 `package` 清单。

### 自动更新发布

- 更新包统一直接上传到 `https://darkeye.win/releases/`，不创建版本号子目录；文件名保留版本号，例如 `https://darkeye.win/releases/Darkeye-1.2.5-macos-arm64-update.tar.zst`。

- 各原生平台构建独立更新器，将本平台产物放入 `resources/updater.exe`（Windows）或 `resources/updater`（macOS）。构建和安装会将它部署到主程序旁。
- 打包前按下文使用 `uv` 安装压缩依赖 `zstandard`。
- `tools/package.py` 额外生成 `Darkeye-版本-平台-架构-update.tar.zst`和包含 SHA-256 的 `latest.json`。Windows 更新 tar.zst 无包装目录；macOS 更新包只包含顶层 `Darkeye.app/`，整份替换应用并清理旧文件。更新包只包含程序，不包含数据或便携配置；包内保留原有相对符号链接和可执行权限，不重复存储链接目标；更新器在解包完成后恢复安全的内部链接；外部链接、循环链接和空包会拒绝生成。tar.zst 通过完整性校验后才替换正式产物，JSON 中的 SHA-256 自动从最终包体计算；安装和便携模式共用它。不要将首次分发的便携包、系统安装包用作更新包。
- 默认更新地址为 `https://darkeye.win/updates/stable/{target}/latest.json`，`{target}` 根据当前应用构建目标替换为 `windows-x64`、`macos-arm64`。可以在随包发布的 `config/update.ini` 修改地址模板。
- 每个平台的清单独立发布，结构为 `latestVersion`、`releaseNotes`、`packages`；`packages` 以目标名为键，每个条目必须有 HTTPS `url` 和真实 `sha256`。发布前填写更新说明，并核对生成的下载地址。
- 先上传版本化更新 tar.zst，确认可下载后，再将生成的 `latest.json` 上传到对应目标目录（不同平台的本地产物也应分目录保存）。不同平台可停留在不同版本。
- Windows 更新根目录为 exe 所在目录，Mac 为 `.app` 根目录（从包内 `Darkeye.app/` 取出完整应用内容替换）；更新器先复制到系统临时目录运行，日志为该临时目录内的 `update.log`。
- 包内文件递归覆盖，包外已有文件保留，`data` 和顶层 `portable.ini` 显式保护。重启时带回原来的数据、配置和知识库路径。
- 程序目录必须可写；系统管理的只读安装位置需通过系统安装程序或包管理器更新。应用不自动提权。

## 使用 uv 安装打包依赖

构建、测试和打包脚本统一通过 `uv run python` 运行。先安装 `uv`，并在项目根目录准备环境：

```sh
uv venv
uv pip install -r tools/requirements-packaging.txt
uv run python tools/package.py
```

`uv venv` 创建项目内的 `.venv`；依赖只需在首次打包或依赖变更后安装。`uv run` 使用该虚拟环境运行脚本，无需手动激活环境。

复用已有 Release 构建时：

```sh
uv run python tools/package.py --skip-build
```

也可追加 `--build-dir` 指定构建目录，或用 `--test` 在构建并测试后打包。`zstandard` 用于生成各平台的 `.tar.zst` 更新包，JSON 和 SHA-256 随包自动生成。

## 准备构建环境

双平台均需安装对应架构的 Qt Core/Gui（含私有头文件）、Widgets、HttpServer、WebSockets、Network、Sql、Svg、ShaderTools；测试还需要 Qt Test。配置 vcpkg，其 baseline 由 `vcpkg.json` 固定。

Windows：

```powershell
$env:VCPKG_ROOT = "C:/src/vcpkg"
$env:Qt6_DIR = "C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6"
uv run python tools/build.py --config Release --test
```

脚本自动导入 MSVC 环境；未设置 VCPKG_ROOT 时尝试使用 Visual Studio 自带 vcpkg。Windows 打包需 7-Zip 和 Inno Setup。

macOS（Apple Silicon）：

```sh
export VCPKG_ROOT="$HOME/src/vcpkg"
export Qt6_DIR="$HOME/Qt/6.12.0/macos/lib/cmake/Qt6"
uv run python tools/build.py --config Release --test
```

预设强制 `CMAKE_OSX_ARCHITECTURES=arm64` 和部署目标 14.4。打包会用 `lipo` 再次核对主程序仅包含 arm64。需要 Xcode Command Line Tools、Ninja、`ditto`、`codesign`、`hdiutil`。


原生界面和 GPU 测试需要桌面会话，不能全部使用 offscreen 替代。系统钥匙串测试在 macOS 默认跳过，只有准备好真实 keyring 后才设置 `DARKEYE_TEST_SYSTEM_KEYRING=1`。

所有平台可用 `--build-dir` 指定独立构建目录、`--clean` 重建、`--dry-run` 预览。macOS 的 CMake 预设为 `macos-arm64-release[-tests]`；Windows 原有预设名称保持兼容。环境中的 Qt6_DIR、CMAKE_TOOLCHAIN_FILE、VCPKG_TARGET_TRIPLET 可以覆盖预设中的默认值。

## 统一打包

在各自原生系统执行：

```sh
uv run python tools/package.py
```

或先构建并测试，再复用已完成的 Release 测试构建：

```sh
# 根据平台选择对应目录：
uv run python tools/package.py --skip-build --build-dir build/windows-msvc-release-tests
uv run python tools/package.py --skip-build --build-dir build/macos-arm64-release-tests
```

`--test` 会在打包前构建和运行全套测试，失败时停止打包。`--skip-build` 明确跳过构建和测试，适合在独立验证通过后使用。两者不能同时使用。

- `--format portable|installer|both`：默认 both。
- `--destination out/某目录/Darkeye-版本-平台-架构`：隔离发布暂存目录与产物。
- 所有清理和输出限制在项目 `out/` 内；带有用户数据的暂存目录以及指向外部的链接不会被自动删除。
- 版本号取自顶层 CMake `project(VERSION)`；更新包 SHA256 写入 `latest.json`，不生成单独的 `.sha256` 文件；成功后清理安装、便携暂存目录及中间文件。
- 安装资源包含项目 LICENSE、cgltf LICENSE、vcpkg 依赖 copyright，以及能从 Qt 安装目录找到的许可证。可通过 CMake `DARKEYE_QT_LICENSE_DIR` 指定 Qt 许可证目录；正式分发前应核对实际使用模块的完整第三方声明。

每个版本产生四个首次分发文件，另有各平台更新包和清单：

```text
Darkeye-版本-windows-x64-portable.7z
Darkeye-版本-windows-x64-setup.exe
Darkeye-版本-macos-arm64-portable.zip
Darkeye-版本-macos-arm64-setup.dmg
```


macOS 默认进行 ad-hoc 签名，适用于构建验证，不等于面向公众的 Developer ID 签名和公证。可通过 `--mac-sign-identity` 指定已在本机钥匙串准备好的 Developer ID 身份。正式发布还需对分发包进行公证和 stapling；本入口不会自动上传或公证。参见 [Apple 分发说明](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution)。

## 原生验证工作流

`.github/workflows/native-release.yml` 提供手动触发的双平台原生构建、全套测试、双格式打包和校验文件上传。仅上传 Actions artifacts，不创建公开 Release。macOS 使用 Apple Silicon runner，Windows 使用 MSVC。

工具自身的主机无关回归测试：

```sh
uv run python tools/test_packaging.py
```

本地 Windows 通过不能替代 macOS 原生结果；必须查看对应 runner 的成功结果和下载包的干净环境运行结果后，才能标记该平台已验证。
