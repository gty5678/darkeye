<div align="center">
  <a href="https://gty5678.github.io/darkeye-webpage/" target="_blank">
    <img src="https://raw.githubusercontent.com/gty5678/darkeye/main/resources/icons/logo.svg" alt="DarkEye" width="128" />
  </a>
  <h1>DarkEye</h1>
  <p><strong>洞察與秩序</strong></p>
  <p>一個純本機的個人媒體資料庫、中繼資料編輯器、關係分析器與歸檔瀏覽器。</p>
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

[📖 線上文件][link-docs]
[🎥 影片介紹][link-video]
[🌐 官網][link-website]
[💬 Discord][link-discord]

</div>

<p align="center">
  <a href="#download">下載與使用</a> •
  <a href="#compliance">合法合規使用聲明</a> •
  <a href="#features">特性</a> •
  <a href="#screenshots">介面預覽</a> •
  <a href="#privacy">隱私與資料</a> •
  <a href="#migration">遷移與匯入</a> •
  <a href="#crawler">抓取說明</a> •
  <a href="#development">開發與技術</a> •
  <a href="#community">社群</a> •
  <a href="#references">參考專案</a>
</p>

<div align="center">
  <a href="https://github.com/gty5678/darkeye/releases" target="_blank">
    <img src="./docs/assets/show.jpg" alt="DarkEye 擬物化 DVD 展示" width="100%" />
  </a>
</div>

<a id="download"></a>

## 下載與使用

支援 **Windows x64** 和 **macOS ARM64（Apple Silicon）**。請在 [Releases][link-releases] 中選擇對應平台的安裝版或便攜版。

<div align="center">
  <a href="https://github.com/gty5678/darkeye/releases">
    <img src="https://img.shields.io/badge/Download-Windows%20x64-blue?style=for-the-badge&logo=windows" alt="下載 Windows x64 版本" />
  </a>
  <a href="https://github.com/gty5678/darkeye/releases">
    <img src="https://img.shields.io/badge/Download-macOS%20ARM64-black?style=for-the-badge&logo=apple" alt="下載 macOS ARM64 版本" />
  </a>
</div>

- **Windows x64**：執行 `.exe` 安裝包，或完整解壓縮 `.7z` 便攜包後執行 `app-<版本>/Darkeye.exe`。
- **macOS ARM64（Apple Silicon）**：執行 `.pkg` 安裝包，安裝後開啟 `/Applications/Darkeye.app`；或完整解壓縮 `.zip` 便攜包，將整個 `DarkEye` 資料夾放在可寫入的位置，再開啟其中的 `Darkeye.app`。

安裝與資料遷移詳情見 [Windows 發布說明](docs/windows-packaging.md) 和 [macOS 使用與打包說明](docs/macos-packaging.md)。

瀏覽器擴充功能請依下方文件安裝**對應瀏覽器的一種**；若發布包附帶 `extensions` 目錄，可直接使用其中的擴充功能，否則從下方入口單獨下載。

### 瀏覽器擴充功能安裝

👉 [線上文件：瀏覽器擴充功能安裝](https://gty5678.github.io/darkeye/usage/#_2)

若發布包已附帶瀏覽器擴充功能，且擴充功能沒有單獨更新，一般**不需要**重複下載。
<div align="center">
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/chrome_capture.zip">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Chrome%2FEdge%20%E6%8F%92%E4%BB%B6-blue?style=for-the-badge" alt="下載 Chrome/Edge 擴充功能" />
  </a>
  　　
  <a href="https://github.com/gty5678/darkeye/releases/download/v1.2.5/firefox_capture.zip">
    <img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-Firefox%20%E6%8F%92%E4%BB%B6-blue?style=for-the-badge" alt="下載 Firefox 擴充功能" />
  </a>
</div>

### 使用說明

👉 [線上文件：使用](https://gty5678.github.io/darkeye/usage/#_3)

### 版本與更新

👉 [常見問題：更新與遷移](https://gty5678.github.io/darkeye/faq/)

設定中可自動更新**軟體本體**；軟體不會自動更新，但是**外掛**會在軟體 `extensions` 目錄更新，需要**手動到瀏覽器重新載入**。外掛另外可在 [Releases][link-releases] 手動下載。

遷移版本時請**更新瀏覽器擴充功能**。抓取器可能因站點變更而很快失效，並會依據回饋人工維護；代理問題無法由軟體解決。只要目標站點能在瀏覽器中開啟，通常即可抓取。

---

<a id="compliance"></a>

## 合法合規使用聲明

- 本工具僅用於管理使用者依法擁有、已獲授權或可合法處理的資料與中繼資訊。
- 使用本工具時，請遵守各國現行法律法規及相關規定。
- 嚴禁將本工具用於非法抓取、侵權傳播、繞過網站存取控制、未經授權處理他人資料等行為。
- 第三方網站內容、介面與存取規則以其平台條款為準，使用者應自行確認並承擔相應合規責任。

---

<a id="features"></a>

## 特性

### 已實現

| **功能** | **說明** | **狀態** |
| -------- | -------- | -------- |
| **資料管理** | 媒體條目、人物與標籤等基礎資料增刪查改 | ✅ |
| **個人記錄** | 自訂記錄條目的手動新增與增刪查改 | ✅ |
| **分析與圖表** | 分析圖表與資料展示（仍有部分未完成功能） | ✅ |
| **擬物化 DVD 盒子陳列** | 擬物化 DVD 陳列與收藏體驗 | ✅ |
| **篩選過濾展示** | 篩選作品頁面 | ✅ |
| **瀏覽器擴充功能** | Chrome / Edge / Firefox 擴充功能，支援沉浸式、互動式的多站點抓取 | ✅ |
| **簡易抓取** | 可用性和品質取決於目標站點的公開政策及存取規則；詳見文件 | ✅ |
| **關聯圖譜** | 檢視關聯；約 1 萬節點下約 60 幀 | ✅ |
| **翻譯** | LLM 翻譯 + 一鍵覆蓋翻譯 | ✅ |
| **本機影片連結** | 若本機已有影片，可將影片連結到資料庫 | ✅ |
| **備份** | 備份系統，用於本機資料歸檔與還原 | ✅ |
| **主題** | 主題切換（3D 場景尚不完全跟隨明／暗） | ✅ |
| **螢幕截圖** | 部分介面支援截圖；女優頁面可按 C 鍵 | ✅ |
| **自動更新** | 自動檢測並下載更新 | ✅ |
| **mdcz NFO 匯入** | [mdcz](https://github.com/ShotHeadman/mdcz) NFO 匯入 | ✅ |
| **Jvedio NFO 匯入** | Jvedio 資料匯出 NFO（測試中） | ✅ |


### 計劃與推進中

| **功能** | **說明** | **狀態** |
| -------- | -------- | -------- |
| **NFO 匯出** | 形成共識後開發；各工具實作不一，目前資料欄位仍不齊 | 🔄 |

長期規劃與更多細項見 [**更新日誌與路線圖**](docs/CHANGELOG.md)（隨開發滾動更新，不代表固定排期）。

- **AI / 工具整合**：探索 CLI 與互動式能力（CHANGELOG 的 `3.x` 路線圖）。
- **同步與共享**：WebDAV、多端備份、UGC 式資訊協作等（`2.x`）。
- **體驗與基礎設施**：持續改進標籤、圖譜、UI、匯出、抓取器與資料庫（`1.x`）。


---

<a id="migration"></a>

## 遷移與匯入

### mdcz 專案 NFO 匯入

已支援 [mdcz](https://github.com/ShotHeadman/mdcz) 產出的 NFO 匯入。

👉 [線上文件：mdcz NFO](https://gty5678.github.io/darkeye/usage/#mdcz-nfo)

### Jvedio 遷移資料

👉 [線上文件：Jvedio](https://gty5678.github.io/darkeye/usage/#jvedio)

---

<a id="privacy"></a>

## 隱私與資料

- **資料與連線**：便攜模式資料由 `portable.ini` 指定：Windows 便攜包使用根目錄共用的 `data/`，macOS 便攜包使用 `.app` 外的 `data/`。安裝版使用系統使用者資料目錄，實際路徑可在軟體設定中查看。遷移便攜版時請保留資料目錄與 `portable.ini`；詳情見 [Windows 發布說明](docs/windows-packaging.md) 和 [macOS 使用與打包說明](docs/macos-packaging.md)。不會主動向第三方上傳你的本機資料；連線主要來自刮削與資源拉取，以及選用的更新下載（Cloudflare R2）、翻譯（Google 或你自備的 LLM API）等。第三方服務由使用者自主啟用並自行承擔合規責任。


---

<a id="screenshots"></a>

## 介面預覽

### 擬物化 DVD

![收藏](docs/assets/dvd.jpg)

![展開](docs/assets/dvd2.jpg)

![女優](docs/assets/actress.jpg)

### 力導向圖

![力導向圖](docs/assets/directforceview.jpg)

### 分析圖表

![圖表](docs/assets/chart.jpg)

### 多作品瀑布流

![多作品](docs/assets/mutiwork.jpg)

### 編輯介面

![編輯介面](docs/assets/edit.jpg)

### 瀏覽器擴充功能（站點範例）

開啟擴充功能後，它會與本機應用程式連線；點擊「新增」即可啟動抓取器並匯入本機。頁面上的「收藏／收錄」等功能僅在連接本機軟體時可用。

![瀏覽器擴充功能連動範例](docs/assets/capture.JPG)

---

<a id="crawler"></a>

## 抓取說明

目前抓取會嘗試取得作品的發布日期、導演、中日標題與簡介、女優和男優（如適用）、標籤、封面、片長、廠商、廠牌、系列、劇照等資訊。

女優資訊主要會取得頭像、出生日期、出道日期、三圍、身高與罩杯、曾用名等（曾用名的更新路徑尚未實作，因此首次以舊名登記時可能出現不一致）。

首次抓取時，目標站點可能會依其存取政策顯示驗證或限制。是否能繼續取決於站點規則及使用者的存取權限。

目前支援多個公開資料站點。實際可用站點會隨版本和目標站點政策變化，請以最新線上文件為準。

---

<a id="development"></a>

## 開發與技術

主要技術以 C++20 / Qt 6、SQLite、本機 HTTP API 與瀏覽器擴充功能協同，並含 QRhi 力導向圖渲染。

若想開發，請先閱讀以下文件，將軟體執行起來。

👉 [開發文件](https://gty5678.github.io/darkeye/development/)

Windows x64 安裝版與便攜版的產生步驟，請參閱 [Windows 打包與發布](docs/windows-packaging.md)。

macOS（Apple Silicon）所需工具、Qt/vcpkg 設定、編譯與啟動命令，以及便攜 ZIP 與安裝 PKG 的產生步驟，請參閱 [macOS 安裝依賴、編譯、執行與打包](docs/macos-packaging.md)。

---

<a id="community"></a>

## 社群

有問題或想法？歡迎加入 Discord：[加入社群][link-discord]

- **新手支援**：文件閱讀中有疑問歡迎提問；線上文件持續完善中。
- **提前獲知進展**：新功能、開發進展與預釋出版本會先在 Discord 討論。
- **參與方向**：想影響 roadmap，歡迎來討論。

---

<a id="references"></a>

## 參考專案

- [mdcz](https://github.com/ShotHeadman/mdcz)（遷移相容參考）
- [Jvedio](https://github.com/hitchao/Jvedio)（遷移相容參考）
- [JavSP](https://github.com/Yuukiy/JavSP)（站點適配思路參考）
- [JAV-JHS](https://sleazyfork.org/zh-CN/scripts/558525-jav-jhs)（資訊整理思路參考）
- [JAV_MovieManager](https://github.com/4evergaeul/JAV_MovieManager)（媒體管理互動參考）
- [stash](https://github.com/stashapp/stash)
- [AMMDS](https://github.com/QYG2297248353/AMMDS-Docker)
- [mdc-ng](https://github.com/mdc-ng/mdc-ng)

---

<a id="license"></a>

## 授權

本專案依 [GNU General Public License v3.0](LICENSE) 授權釋出。

---
## 貢獻者

<a href="https://github.com/gty5678/darkeye/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=gty5678/darkeye" alt="Contributors" width="500" />
</a>

---

<div align="center" style="color: gray;">DarkEye — 本機資料書架，安全自管。</div>

<!-- Badge images -->

[badge-readme-zh-CN]: https://img.shields.io/badge/README%20%C2%B7%20%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-555555?style=for-the-badge
[badge-readme-zh-TW]: https://img.shields.io/badge/README%20%C2%B7%20%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87-2ea44f?style=for-the-badge
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
