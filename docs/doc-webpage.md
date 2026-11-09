# 文档与网页

DarkEye 的文档网页由 MkDocs 和 Material 主题生成。修改 Markdown 源文件后，在本地通过 `uv run mkdocs gh-deploy` 构建并推送网页。当前不使用仓库中失败的文档 Actions 工作流作为发布方式。

## 1. 文件和网页的位置

| 内容 | 仓库内的位置 / 地址 |
| --- | --- |
| 文档源文件 | `docs/`，每个 `.md` 文件对应一个页面 |
| 当前页面 | `docs/doc-webpage.md` |
| 网站名称、导航、主题配置 | `mkdocs.yml` |
| Python 文档依赖 | `requirements-docs.txt` |
| 网页发布分支 | GitHub 仓库的 `gh-pages` 分支，由 `mkdocs gh-deploy` 生成并推送 |
| 本地构建产物 | `site/`，已被 Git 忽略，无需提交 |
| 在线文档首页 | <https://gty5678.github.io/darkeye/> |
| 当前页面的发布地址 | <https://gty5678.github.io/darkeye/doc-webpage/> |

以下命令都在仓库根目录执行，也就是包含 `mkdocs.yml` 的目录。

## 2. 安装文档环境

需要 uv 和 Git。文档构建不需要安装 Qt 或编译 C++ 程序。

安装 uv（已经安装的可以跳过）：

### macOS / Linux

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### Windows PowerShell

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

安装后重新打开终端，在仓库根目录创建环境并安装文档依赖：

```bash
uv venv --python 3.13
uv pip install -r requirements-docs.txt
```

以上命令使用仓库根目录的 `.venv` 环境，后续直接使用 `uv run`，无需手动激活。不要提交虚拟环境文件。

## 3. 修改、预览和构建

修改 `docs/` 中对应的 Markdown 文件。新增页面时，还要在 `mkdocs.yml` 的 `nav` 中加入入口，例如：

```yaml
nav:
  - 文档与网页: doc-webpage.md
```

这里的路径相对于 `docs/`，不要再加 `docs/` 前缀；冒号后需要保留空格。以上只是新增导航项的示例，实际修改时保留其他已有导航项。

启动本地预览：

```bash
uv run mkdocs serve
```

浏览器打开 <http://127.0.0.1:8000/>。保存 Markdown 或配置后，预览会自动刷新；在终端按 `Ctrl+C` 停止。

需要严格检查文档配置和链接时执行：

```bash
uv run mkdocs build --strict
```

构建成功后，静态网页位于 `site/`。严格检查会将警告视为错误；普通发布命令未启用严格模式，仍应查看输出中的警告并修复相关问题。

## 4. 推送和发布

先确认当前分支与改动范围：

```bash
git branch --show-current
git status --short
git diff -- docs/doc-webpage.md mkdocs.yml
```

以提交本页及导航修改为例：

```bash
git add docs/doc-webpage.md mkdocs.yml
git commit -m "docs: add documentation website guide"
git push origin HEAD
```

按实际修改的文件调整 `git add` 列表。以上 Git 命令保存并推送文档源码；网页发布使用下面的命令：

```bash
uv run mkdocs gh-deploy
```

该命令会构建当前本地文档，将生成的静态网页提交到 `gh-pages` 分支并推送到 `origin`，需要当前 Git 账号有仓库推送权限。它使用当前工作区的内容，发布前应确认改动范围；无需手动提交 `site/`，也无需切换到 `gh-pages` 分支。

GitHub 仓库的 **Settings → Pages → Build and deployment → Source** 应选择 **Deploy from a branch**，分支选择 **`gh-pages`**，目录选择 **`/ (root)`**。首次发布生成该分支后即可选择。

命令成功后，等待 GitHub Pages 更新，再打开[在线文档](https://gty5678.github.io/darkeye/)检查内容；如浏览器仍显示旧内容，可尝试强制刷新。`.github/workflows/docs.yml` 中现有的文档工作流目前失败，不作为本页发布流程的依据。

## 5. 常见问题

- **提示找不到 `mkdocs`**：确认在仓库根目录执行命令，已经创建 `.venv`，并重新执行 `uv pip install -r requirements-docs.txt`。
- **本地端口被占用**：使用 `uv run mkdocs serve -a 127.0.0.1:8001`，然后访问 <http://127.0.0.1:8001/>。
- **页面没有出现在导航中**：检查 `mkdocs.yml` 的 `nav` 是否包含该文件，路径和大小写是否正确。
- **源码已推送但网页没更新**：还需要执行 `uv run mkdocs gh-deploy`，并确认 Pages 使用 `gh-pages` 分支的根目录。
- **发布失败**：查看本地命令的错误输出。构建错误需修复文档或配置；Git 推送错误需检查 `origin` 地址、SSH / HTTPS 认证和仓库写入权限。
