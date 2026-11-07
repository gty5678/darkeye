# Windows 运行模式与发布包

运行模式与安装方式分别处理。同一个 `Darkeye.exe` 可以在任意目录运行：标准模式使用系统用户数据目录，便携模式使用明确指定的数据目录。安装程序只负责部署程序、快捷方式和卸载入口。

## 标准模式

没有 `portable.ini` 或显式数据目录覆盖时，使用 Qt `AppLocalDataLocation`。当前应用标识下，Windows 路径为 `%LOCALAPPDATA%\Darkeye\Darkeye`。移动程序文件不会改变数据库和用户设置的位置。config 和 avwiki 默认从 exe 同级读取。

安装版默认部署到 `%LOCALAPPDATA%\Programs\Darkeye`，当前用户安装无需管理员权限，提供开始菜单、可选桌面快捷方式和“已安装的应用”卸载入口。用户可以更改程序安装目录；卸载保留用户数据。

不经过安装程序、直接把标准程序目录复制到其他位置，也仍然是标准模式。

## 便携模式：版本程序与数据解耦

便携发布包解压后的结构如下。示例版本号用于说明，实际版本号来自 CMake。

```text
DarkEye/
├── app-1.5.0/
│   ├── Darkeye.exe
│   ├── portable.ini        # DataDirectory=../data
│   ├── config/             # 随版本发布的只读配置
│   ├── avwiki/             # 当前版本使用的知识库
│   └── ...依赖文件
├── app-1.6.0/              # 同样包含 exe 同级 portable.ini
└── data/
    ├── settings.ini
    ├── public/
    ├── private/
    ├── cache/
    ├── temp/
    └── logs/
```

`portable.ini` 明确指定数据目录：

```ini
[Portable]
DataDirectory=../data
```

相对路径以 **portable.ini 所在目录** 为根，与当前工作目录和版本目录名无关。也可以填写 `profiles/user-a`、`../shared-data` 或用户指定的绝对路径。需要整个目录可搬迁时应使用相对路径。

只检查 exe 同级的 `portable.ini`，不检查父目录。各 `app-<版本>` 分别包含标记，默认通过 `../data` 共用上级数据目录；只有标记而未指定 DataDirectory 时也默认使用 `../data`。

显式配置损坏、`Portable/DataDirectory` 显式为空，或目标目录不可写时，启动失败并提示；不能悄悄退回系统目录或创建另一套数据库。标准安装程序也拒绝安装到会被识别为便携模式的位置。

路径选择优先级：`--data-dir`（或测试/工具的构造参数） → 兼容的 `DARKEYE_DATA_DIR` 环境变量 → exe 同级 `portable.ini` → 系统用户数据目录。环境变量相对值以程序目录解析；正常发布不设置环境变量。便携模式由 exe 同级标记判断，路径覆盖本身不作为便携标记。

启动参数为 `--config-dir <目录>`、`--avwiki-dir <目录>`、`--data-dir <目录>`，可分别指定，支持 `--参数=目录`。参数中的相对路径在启动时以工作目录转换成绝对路径；缺少参数值或空值会终止启动。未指定的 config 和 avwiki 分别为 `<exe目录>/config`、`<exe目录>/avwiki`。config 是资源配置目录，用户 `settings.ini` 仍位于 data。

应用管理的数据库、配置、图片、备份、JSON、日志、缓存及临时下载目录均基于选定的数据目录。系统网络缓存、操作系统临时文件和安全凭据存储不保证完全没有系统痕迹；WebDAV 凭据目前仍由系统凭据管理器保存，移动设备后可能需要重新登录。

## 升级与版本切换

新便携版本只增加包含 `portable.ini` 的 `app-<新版本>`，保留整个上级 `data`。从新发布包中只复制新版本的程序目录；已有版本的标记不覆盖。直接运行对应版本目录中的 `Darkeye.exe` 即可。

删除不再使用的版本时只删除对应 `app-<版本>`；备份时复制 `data`、知识库以及各版本的 `portable.ini`，完整离线备份可复制整个 `DarkEye` 目录。

不要同时启动两个版本访问同一数据目录。程序目录可回退并不代表数据库可以降级；新版本迁移数据库前应备份，旧版是否能读取新数据库取决于数据库兼容性。

标准模式和便携模式共用 `-update.tar.zst` 程序更新包，原目录递归覆盖，包外文件保留；不修改已有 `portable.ini` 或用户数据。自动更新使用随包部署的 `updater.exe`，从系统临时目录启动，不再调用旧的 `DarkEyeUpdater.exe`。首次分发的便携完整包和安装程序不作为自动更新包。发布步骤见[自动更新发布](cross-platform-packaging.md#自动更新发布)。

## 数据内部路径规则

新配置包含 `Paths/Version=2`。`Paths/Database=public/public.db` 等相对路径从数据目录解析；绝对路径保持原样。旧配置第一次启动时去掉旧的 `data/` 前缀，其他自定义相对路径转换成原程序目录下的绝对路径。迁移只调整配置，不移动外部文件。

知识库直接使用 `--avwiki-dir` 指定的目录，未指定时使用 exe 同级 `avwiki`，不复制到 data。编辑、保存和知识库更新都操作该目录；更换版本前需要自行保留知识库修改。

<a id="windows-packaging"></a>

## Windows 安装版与便携版的生成方法

### 1. 准备环境

在 Windows x64 上准备以下工具，具体编译环境配置见 [开发准备](development.md)：

| 工具 | 用途 | 当前脚本要求 |
| --- | --- | --- |
| Python 3.9+ | 执行 `tools/package.py`、`tools/build.py` | 通过 `uv` 创建虚拟环境，并安装 `tools/requirements-packaging.txt` 中的压缩依赖 |
| Visual Studio C++ 工具链 | 编译主程序 | MSVC x64 和 Windows SDK；脚本自动加载开发环境 |
| CMake、Ninja、Qt、vcpkg 依赖 | 配置、构建与运行库部署 | CMake 3.25+；当前预设使用 Qt 6.12.0 MSVC 2022 64-bit kit |
| 7-Zip | 生成并校验便携压缩包 | PATH 中的 `7zz` / `7z` / `7za`，或常见 Program Files 下的 `7-Zip/7z.exe` |
| Inno Setup | 编译 Windows 安装程序 | `ISCC.exe`，安装脚本为 `tools/installer.iss` |

Windows 预设固定使用 `C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6`；Qt 安装在其他位置时请修改预设或通过 CMake 的 `-DQt6_DIR=...` 覆盖。先在终端设置 `VCPKG_ROOT`，例如：

```powershell
$env:VCPKG_ROOT = "C:/src/vcpkg"
```

本机已安装 Inno Setup 6.7.3，安装目录为 `C:/Program Files (x86)/Inno Setup 6/`，编译器完整路径为 `C:/Program Files (x86)/Inno Setup 6/ISCC.exe`。当前打包脚本可以自动找到该路径，无需额外配置。建议将 Inno Setup 安装在项目外的固定目录，避免清理 `out/` 时一并删除编译器及其配套文件。

Inno Setup 编译器按以下顺序查找：命令行 `--iscc` 指定路径、PATH 中的 `ISCC`、项目内 `out/tooling/inno/ISCC.exe`、`C:/Program Files (x86)/Inno Setup 6/ISCC.exe`、`C:/Program Files/Inno Setup 7/ISCC.exe`。编译器装在其他位置时使用 `--iscc` 指定。

### 2. 在仓库根目录执行打包

打开 PowerShell，切换到包含根 `CMakeLists.txt` 和 `tools` 的仓库目录，执行：

先在项目根目录安装打包依赖（需已安装 `uv`）：

```powershell
uv venv
uv pip install -r tools/requirements-packaging.txt
```

使用虚拟环境运行打包脚本，无需手动激活。后续示例同样使用 `uv run --no-project`，详见[跨平台依赖安装说明](cross-platform-packaging.md#使用-uv-安装打包依赖)。

```powershell
# 默认：编译 Release，生成便携包和安装包
uv run --no-project python tools/package.py
```

脚本自动使用 `windows-msvc-release` 配置预设和 `release` 构建预设，构建目录为 `build/windows-msvc-release`。默认增量编译，不会每次清空整个构建目录。

只生成其中一种：

```powershell
# 便携版：只需要 7-Zip，不需要 Inno Setup
uv run --no-project python tools/package.py --format portable

# 安装版：只需要 Inno Setup，不需要 7-Zip
uv run --no-project python tools/package.py --format installer

# 指定 Inno Setup 编译器位置
uv run --no-project python tools/package.py --format installer --iscc "C:/path/to/ISCC.exe"
```

其他常用方式：

```powershell
# 已完成当前源码的 release 构建，只重新暂存、部署依赖和打包
uv run --no-project python tools/package.py --skip-build

# 清除发布构建目录并全量编译，再生成两个包
uv run --no-project python tools/package.py --clean

# 只显示步骤，不执行构建、覆盖或打包
uv run --no-project python tools/package.py --dry-run
```

`--skip-build` 固定读取 `build/windows-msvc-release`。源码或版本号修改后，应先正常构建再打包，避免使用旧程序。

### 3. 获取生成的文件

版本号自动读取根 `CMakeLists.txt` 中的 `project(Darkeye VERSION ...)`，不需要单独修改打包脚本。

| 产物 | 默认输出位置 | 生成工具 |
| --- | --- | --- |
| 便携版 | `out/Darkeye-<版本>-windows-x64-portable.7z` | 7-Zip |
| 安装版 | `out/Darkeye-<版本>-windows-x64-setup.exe` | Inno Setup |

例如当前版本 `1.2.5`：

```text
out/
├── Darkeye-1.2.5-windows-x64-portable.7z
└── Darkeye-1.2.5-windows-x64-setup.exe
```

**目前便携版格式为 `.7z`，不是 `.zip`。** 安装包生成后不会自动在本机安装；运行 `setup.exe` 才会创建快捷方式和卸载注册项。

便携包解压后顶层为 `DarkEye`：

```text
DarkEye/
├── app-1.2.5/
│   ├── portable.ini    # [Portable] DataDirectory=../data
│   ├── Darkeye.exe
│   └── ...依赖、只读配置和初始知识库
└── data/              # 发布包中为空，运行后保存用户数据
```

两种正式发布包均不包含开发机的数据库、图片、个人配置或 `seed-data`。标准安装包也不含便携配置或标记。发布前须提供本平台的 `resources/updater.exe`；打包同时生成用于自动更新的程序 ZIP 和平台清单。

### 4. 脚本实际执行的步骤

1. 用 CMake 配置、编译 Release 主程序。
2. 执行 `cmake --install`，将程序及 Qt、第三方 DLL 暂存到 `out/Darkeye-<版本>-windows-x64-install`。
3. 检查必要文件，拒绝把便携配置或开发机数据混入标准程序目录，并清理不用的运行库和工具元数据。
4. 生成便携版时，将同一份程序复制到 `out/Darkeye-<版本>-windows-x64-portable/DarkEye/app-<版本>`，创建 exe 同级 `portable.ini`（`DataDirectory=../data`）和上级空 `data`，调用 7-Zip 压缩，再用 `7z t` 校验完整性。
5. 生成安装版时，调用 `ISCC.exe` 编译 `tools/installer.iss`，把标准程序暂存目录封装为安装 exe。

运行库清理规则位于 `tools/package.py`：剔除 VC 安装程序、软件 OpenGL 库、非 SQLite 的 SQL 驱动、QML 调试插件和 `.qmltypes` 工具元数据。当前 Windows 渲染使用 Direct3D 11，还会剔除 Direct3D 12 Shader Model 6 使用的 `dxcompiler.dll`、`dxil.dll`，以及未启用的 TUIO 触摸输入插件 `plugins/generic/qtuiotouchplugin.dll`。保留 Direct3D 11 编译着色器需要的 `d3dcompiler_47.dll`，以及图片格式、SVG 图标、TLS、网络状态、Windows 平台和样式插件。如果以后启用 Direct3D 12 Shader Model 6 或 TUIO，需要同步调整剔除规则。

重新打包会覆盖相应的发布暂存目录和同名产物。便携暂存目录若已含用户数据，脚本会拒绝清理；日常使用应解压到单独位置，不要直接长期运行暂存目录中的程序。

### 5. CMake Install 与正式发布打包

CMake 配置预设为 `windows-msvc-debug`、`windows-msvc-debug-tests`、`windows-msvc-release`、`windows-msvc-release-tests`，对应的构建预设为 `debug`、`debug-tests`、`release`、`release-tests`。四个预设的 Install 都只部署程序、运行依赖、config 和 avwiki，不携带开发数据，也不生成便携标记。

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release --target Darkeye
cmake --install build/windows-msvc-release --config Release
```

默认输出到 `out/install/release`，程序同级包含 `config/` 和 `avwiki/`。`tools/package.py` 在此标准程序布局上封装安装包；生成便携包时再增加 exe 同级的 `portable.ini` 和上级空 data，均不复制开发数据。

普通开发构建不生成 `portable.ini`，也不在 exe 同级创建 `data`。Visual Studio 的普通调试目标通过三个启动参数使用项目根目录的 config、avwiki、data；Install 目标不传参数。直接双击无标记的 build 目录 exe 时使用系统用户数据目录。`tools/run.ps1` 也默认通过三个参数使用项目目录，支持 `-ConfigDirectory`、`-AvwikiDirectory`、`-DataDirectory` 单独覆盖。

## 从旧便携目录迁移

退出旧程序并备份数据，把旧 `<exe目录>/data` 整体复制到新 `DarkEye/data`。若旧版知识库尚在 exe 同级 `avwiki` 且含有修改，把它复制到 `DarkEye/app-<版本>/avwiki`，或通过 `--avwiki-dir` 指定保留目录。确认 exe 同级 `portable.ini` 指向 `../data`，再启动 `app-<版本>/Darkeye.exe`。

如果旧配置包含依赖旧程序目录的自定义相对路径，应在迁移前改为绝对路径，或迁移后重新配置。新版本的相对路径都应基于共享数据目录。

## 后续平台移植

标准模式数据位置通过 Qt `AppLocalDataLocation` 获取；便携配置与版本目录规则不依赖 Windows API。Linux 的共享资源布局、macOS 的 `.app/Contents/Resources` 布局，以及对应平台的包、依赖部署和签名，仍需在目标系统验证。
