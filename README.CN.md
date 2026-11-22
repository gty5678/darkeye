<div align="center">
  <a href="https://gty5678.github.io/darkeye-webpage/" target="_blank">
    <img src="https://raw.githubusercontent.com/gty5678/darkeye/main/resources/icons/logo.svg" alt="DarkEye" width="128" />
  </a>
  <h1>DarkEye</h1>
  <p><strong>洞察与秩序</strong></p>
  <p>一个纯本地的个人媒体资料库、元数据编辑器、关系分析器和归档浏览器。</p>
  <br />

[![README · 日本語][badge-readme-ja]](README.md)
[![README · 简体中文][badge-readme-zh-CN]](README.CN.md)
[![README · 繁體中文][badge-readme-zh-TW]](README.zh-TW.md)

![Qt 6.12.0][badge-qt]
![C++][badge-cpp]
![CMake 3.25+][badge-cmake]
![MSVC 2022][badge-msvc]
![Apple Clang][badge-clang]
[![SQLite][badge-sqlite]](https://sqlite.org/)
![Platform][badge-platform]
![License][badge-license]
![GitHub last commit][badge-last-commit]
![GitHub release][badge-release]
![GitHub Repo stars][badge-stars]
![GitHub all releases][badge-downloads]

<br />

[📖 在线文档][link-docs]
[🎥 视频介绍][link-video]
[🌐 官网][link-website]
[💬 Discord][link-discord]

</div>

<p align="center">
  <a href="#download">下载与使用</a> •
  <a href="#compliance">合法合规使用声明</a> •
  <a href="#features">特性</a> •
  <a href="#screenshots">界面预览</a> •
  <a href="#privacy">隐私与数据</a> •
  <a href="#migration">迁移与导入</a> •
  <a href="#crawler">抓取说明</a> •
  <a href="#development">开发与技术</a> •
  <a href="#community">社群</a> •
  <a href="#references">参考项目</a>
</p>

<div align="center">
  <a href="https://github.com/gty5678/darkeye/releases" target="_blank">
    <img src="./docs/assets/show.jpg" alt="DarkEye 拟物化 DVD 展示" width="100%" />
  </a>
</div>

<a id="download"></a>

## 下载与使用

支持 **Windows x64** 和 **macOS ARM64（Apple Silicon）**。请在 [Releases][link-releases] 中选择对应平台的安装版或便携版。

<div align="center">
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/Darkeye-1.2.5-windows-x64-setup.exe">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Windows%20x64%20%E5%AE%89%E8%A3%85%E7%89%88-blue?style=for-the-badge&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI%2BPHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0wIDBoMTF2MTFIMHptMTMgMGgxMXYxMUgxM3pNMCAxM2gxMXYxMUgwem0xMyAwaDExdjExSDEzeiIvPjwvc3ZnPg%3D%3D" alt="下载 Windows x64 安装版" />
  </a>
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/Darkeye-1.2.5-windows-x64-portable.7z">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Windows%20x64%20%E4%BE%BF%E6%90%BA%E7%89%88-blue?style=for-the-badge&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI%2BPHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0wIDBoMTF2MTFIMHptMTMgMGgxMXYxMUgxM3pNMCAxM2gxMXYxMUgwem0xMyAwaDExdjExSDEzeiIvPjwvc3ZnPg%3D%3D" alt="下载 Windows x64 便携版" />
  </a>
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/Darkeye-1.2.5-macos-arm64-setup.dmg">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-macOS%20ARM64%20%E5%AE%89%E8%A3%85%E7%89%88-black?style=for-the-badge&logo=apple" alt="下载 macOS ARM64 安装版" />
  </a>
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/Darkeye-1.2.5-macos-arm64-portable.zip">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-macOS%20ARM64%20%E4%BE%BF%E6%90%BA%E7%89%88-black?style=for-the-badge&logo=apple" alt="下载 macOS ARM64 便携版" />
  </a>
</div>

- **Windows x64 安装版**：运行 `.exe` 安装包。
- **Windows x64 便携版**：完整解压 `.7z` 便携包后运行 `app-<版本>/Darkeye.exe`。
- **macOS ARM64 安装版（Apple Silicon）**：打开 `.dmg`，将 `Darkeye.app` 拖入 `Applications`，然后打开 `/Applications/Darkeye.app`。
- **macOS ARM64 便携版（Apple Silicon）**：完整解压 `.zip` 便携包，将整个 `DarkEye` 文件夹放在可写位置，再打开其中的 `Darkeye.app`。

安装与数据迁移详情见 [Windows 打包说明](docs/windows-packaging.md) 和 [macOS 打包说明](docs/macos-packaging.md)。

### 桥接器与浏览器扩展安装

👉 [在线文档：浏览器扩展安装](https://gty5678.github.io/darkeye/usage/#_2)

项目采用分离设计，信息获取服务完全拆分到[mosaic](https://github.com/gty5678/mosaic.git)项目中

<div align="center">
  <a href="https://github.com/gty5678/mosaic/releases/download/v1.0/mosic-win-x64.exe">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Windows%20x64%20%E6%A1%A5%E6%8E%A5%E5%99%A8-blue?style=for-the-badge&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI%2BPHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0wIDBoMTF2MTFIMHptMTMgMGgxMXYxMUgxM3pNMCAxM2gxMXYxMUgwem0xMyAwaDExdjExSDEzeiIvPjwvc3ZnPg%3D%3D" alt="下载 Windows x64 桥接器" />
  </a>
  <a href="https://github.com/gty5678/mosaic/releases/download/v1.0/mosic-macos-arm64">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-macOS%20ARM64%20%E6%A1%A5%E6%8E%A5%E5%99%A8-black?style=for-the-badge&logo=apple" alt="下载 macOS ARM64 桥接器" />
  </a>
  <a href="https://github.com/gty5678/mosaic/releases/download/v1.0/firefox.zip">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Firefox%20%E6%B5%8F%E8%A7%88%E5%99%A8%E6%89%A9%E5%B1%95-orange?style=for-the-badge&logo=firefoxbrowser" alt="下载 Firefox 浏览器扩展" />
  </a>
  <a href="https://github.com/gty5678/mosaic/releases/download/v1.0/chrome.zip">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Chrome%20%2F%20Edge%20%E6%B5%8F%E8%A7%88%E5%99%A8%E6%89%A9%E5%B1%95-blue?style=for-the-badge&logo=googlechrome" alt="下载 Chrome / Edge 浏览器扩展" />
  </a>
</div>

### 使用说明

👉 [在线文档：使用](https://gty5678.github.io/darkeye/usage/#_3)

### 版本与更新

👉 [常见问题：更新与迁移](https://gty5678.github.io/darkeye/faq/)

设置中可自动更新**软件本体**；软件不会自动更新，但是**插件**会在软件`extensions` 目录更新，需要**手动去浏览器重新加载**。插件另外可在[Releases][link-releases] 手动下载。

迁移版本时请**更新浏览器插件**。抓取器可能因站点变更而很快失效，并会依据反馈人工维护；代理问题无法由软件解决。只要目标站点能在浏览器中打开，通常即可抓取。

---

<a id="compliance"></a>

## 合法合规使用声明

- 本工具仅用于管理用户依法拥有、已获授权或可合法处理的数据与元信息。
- 使用本工具时，请遵守各国现行法律法规及相关规定。
- 严禁将本工具用于非法抓取、侵权传播、绕过网站访问控制、未经授权处理他人数据等行为。
- 第三方网站内容、接口与访问规则以其平台条款为准，用户应自行确认并承担相应合规责任。

---

<a id="features"></a>

## 特性

### 已实现

| **功能** | **说明** | **状态** |
| -------- | -------- | -------- |
| **数据管理** | 媒体条目、人物与标签等基础数据增删查改 | ✅ |
| **个人记录** | 自定义记录条目的手动添加与增删查改 | ✅ |
| **分析与图表** | 分析图表与数据展示（仍有部分未完成功能） | ✅ |
| **拟物化DVD盒子陈列** | 拟物化 DVD 陈列与收藏体验 | ✅ |
| **筛选过滤展示** | 筛选作品页面 | ✅ |
| **浏览器扩展** | Chrome / Edge / Firefox 扩展，支持沉浸式、互动式的多站点抓取 | ✅ |
| **简易抓取** | 可用性和质量取决于目标站点的公开政策及访问规则；详见文档 | ✅ |
| **关联图谱** | 查看关联；约 1 万节点下约 60 帧 | ✅ |
| **翻译** | LLM 翻译 + 一键覆盖翻译 | ✅ |
| **本地视频链接** | 如果本地存在视频可将视频链接到数据库中 | ✅ |
| **备份** | 备份系统，用于本地资料归档与恢复 | ✅ |
| **主题** | 主题切换（3D 场景尚不完全跟随时明/暗） | ✅ |
| **截图** | 部分界面支持截图；女优页面可按 C 键 | ✅ |
| **自动更新** | 自动检测并下载更新 | ✅ |
| **mdcz NFO导入** | [mdcz](https://github.com/ShotHeadman/mdcz)  NFO 导入 | ✅ |
| **Jvedio NFO导入** | Jvedio 数据导出 NFO（测试中） | ✅ |


### 计划与推进中

| **功能** | **说明** | **状态** |
| -------- | -------- | -------- |
| **NFO 导出** | 形成共识后开发；各工具实现不一，当前数据字段仍不齐 | 🔄 |

长期规划与更多细项见 [**更新日志与路线图**](docs/CHANGELOG.md)（随开发滚动更新，不代表固定排期）。

- **AI / 工具集成**：探索 CLI 和交互式能力（CHANGELOG 的 `3.x` 路线图）。
- **同步与共享**：WebDAV、多端备份、UGC 式信息协作等（`2.x`）。
- **体验与基础设施**：持续改进标签、图谱、UI、导出、抓取器与数据库（`1.x`）。


---

<a id="migration"></a>

## 迁移与导入

### mdcz 项目 NFO 导入

已支持 [mdcz](https://github.com/ShotHeadman/mdcz) 产出的 NFO 导入。

👉 [在线文档：mdcz NFO](https://gty5678.github.io/darkeye/usage/#mdcz-nfo)

### Jvedio 迁移数据

👉 [在线文档：Jvedio](https://gty5678.github.io/darkeye/usage/#jvedio)

---

<a id="privacy"></a>

## 隐私与数据

- **数据与联网**：便携模式数据由 `portable.ini` 指定：Windows 便携包使用根目录共享的 `data/`，macOS 便携包使用 `.app` 外的 `data/`。安装版使用系统用户数据目录，实际路径可在软件设置中查看。迁移便携版时请保留数据目录与 `portable.ini`；详情见 [Windows 发布说明](docs/windows-packaging.md) 和 [macOS 使用与打包说明](docs/macos-packaging.md)。不会主动向第三方上传你的本地资料；联网主要来自刮削与资源拉取，以及可选的更新下载（Cloudflare R2）、翻译（Google 或你自配的 LLM API）等。第三方服务由用户自主启用并自行承担合规责任。


---

<a id="screenshots"></a>

## 界面预览

### 拟物化 DVD

![收藏](docs/assets/dvd.jpg)

![展开](docs/assets/dvd2.jpg)

![女优](docs/assets/actress.jpg)

### 力导向图

![力导向图](docs/assets/directforceview.jpg)

### 分析图表

![图表](docs/assets/chart.jpg)

### 多作品瀑布流

![多作品](docs/assets/mutiwork.jpg)

### 编辑界面

![编辑界面](docs/assets/edit.jpg)

### 浏览器扩展（站点示例）

打开扩展后，它会与本地应用连接；点击“添加”即可启动抓取器并导入本地。页面上的“收藏／收录”等功能仅在连接本机软件时可用。

![浏览器扩展联动示例](docs/assets/capture.JPG)

---

<a id="crawler"></a>

## 抓取说明

当前抓取会尝试获取作品的发布日期、导演、中日标题与简介、女优和男优（如适用）、标签、封面、片长、厂商、厂牌、系列、剧照等信息。

女优信息主要会获取头像、出生日期、出道日期、三围、身高与罩杯、曾用名等（曾用名的更新路径尚未实现，因此首次以旧名登记时可能出现不一致）。

首次抓取时，目标站点可能会依据其访问政策显示验证或限制。是否能继续取决于站点规则及使用者的访问权限。

当前支持多个公开数据站点。实际可用站点会随着版本和目标站点政策变化，请以最新在线文档为准。

---

<a id="development"></a>

## 开发与技术
主要技术基于 C++20 / Qt 6、SQLite、本地 HTTP API 与浏览器扩展协同，并含 QRhi 力导向图渲染。

若想开发，请先阅读下面的文档将软件运行起来。
👉 [开发文档](https://gty5678.github.io/darkeye/development/)

Windows x64 安装版与便携版的生成步骤见 [Windows 打包与发布](docs/windows-packaging.md)。

macOS（Apple Silicon）需要安装的工具、Qt/vcpkg 配置、编译与启动命令，以及便携 ZIP 和安装 PKG 的生成步骤见 [macOS 安装依赖、编译、运行与打包](docs/macos-packaging.md)。已有本机环境可直接使用文档第 0 节的命令。

---

<a id="community"></a>

## 社群

有问题或想法？欢迎加入 Discord：[加入社群][link-discord]

- **新手支持**：文档阅读中有疑问欢迎提问；在线文档持续完善中。
- **提前获知进展**：新功能、开发进展与预发布版本会先在 Discord 讨论。
- **参与方向**：想影响 roadmap，欢迎来讨论。

---

<a id="references"></a>

## 参考项目

- [mdcz](https://github.com/ShotHeadman/mdcz)（迁移兼容参考）
- [Jvedio](https://github.com/hitchao/Jvedio)（迁移兼容参考）
- [JavSP](https://github.com/Yuukiy/JavSP)（站点适配思路参考）
- [JAV-JHS](https://sleazyfork.org/zh-CN/scripts/558525-jav-jhs)（信息整理思路参考）
- [JAV_MovieManager](https://github.com/4evergaeul/JAV_MovieManager)（媒体管理交互参考）
- [stash](https://github.com/stashapp/stash)
- [AMMDS](https://github.com/QYG2297248353/AMMDS-Docker)
- [mdc-ng](https://github.com/mdc-ng/mdc-ng)

---

<a id="license"></a>

## 许可证

本项目以 [GNU General Public License v3.0](LICENSE) 授权发布。

---
## 贡献者

<a href="https://github.com/gty5678/darkeye/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=gty5678/darkeye" alt="Contributors" width="500" />
</a>

---

<div align="center" style="color: gray;">DarkEye — 本地资料书架，安全自管。</div>

<!-- Badge images -->

[badge-readme-zh-CN]: https://img.shields.io/badge/README%20%C2%B7%20%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-2ea44f?style=for-the-badge
[badge-readme-zh-TW]: https://img.shields.io/badge/README%20%C2%B7%20%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87-555555?style=for-the-badge
[badge-readme-ja]: https://img.shields.io/badge/README%20%C2%B7%20%E6%97%A5%E6%9C%AC%E8%AA%9E-555555?style=for-the-badge
[badge-qt]: https://img.shields.io/badge/Qt-6.12.0-41CD52?logo=qt&logoColor=white
[badge-cpp]: https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus
[badge-cmake]: https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=cmake&logoColor=white
[badge-msvc]: https://img.shields.io/badge/MSVC-2022-5C2D91?logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB2aWV3Qm94PSIwIDAgMjQgMjQiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI%2BPHBhdGggZmlsbD0iI0YyNTAyMiIgZD0iTTEgMWgxMHYxMEgxeiIvPjxwYXRoIGZpbGw9IiM3RkJBMDAiIGQ9Ik0xMyAxaDEwdjEwSDEzeiIvPjxwYXRoIGZpbGw9IiMwMEE0RUYiIGQ9Ik0xIDEzaDEwdjEwSDF6Ii8%2BPHBhdGggZmlsbD0iI0ZGQjkwMCIgZD0iTTEzIDEzaDEwdjEwSDEzeiIvPjwvc3ZnPg%3D%3D
[badge-sqlite]: https://img.shields.io/badge/SQLite-local%20storage-003B57?logo=sqlite&logoColor=white
[badge-platform]: https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20macOS%20ARM64-0078D4
[badge-license]: https://img.shields.io/github/license/gty5678/darkeye
[badge-last-commit]: https://img.shields.io/github/last-commit/gty5678/darkeye
[badge-release]: https://img.shields.io/github/v/release/gty5678/darkeye
[badge-stars]: https://img.shields.io/github/stars/gty5678/darkeye?style=social
[badge-downloads]: https://img.shields.io/github/downloads/gty5678/darkeye/total

<!-- Links -->

[link-docs]: https://gty5678.github.io/darkeye/
[link-video]: https://youtu.be/VCsw1D0ccgY?si=e9typx4kPnzaVFZq
[link-website]: https://gty5678.github.io/darkeye-webpage/
[link-discord]: https://discord.gg/3thnEguWUk
[link-releases]: https://github.com/gty5678/darkeye/releases

[badge-clang]: https://img.shields.io/badge/Apple%20Clang-arm64-black?logo=apple
