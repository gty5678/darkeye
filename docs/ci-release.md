# GitHub Actions 发布

`Native Release` 在 Windows x64 和 macOS ARM64 完成构建、测试、打包后，统一发布。

| 产物 | 去向 |
| --- | --- |
| `*-portable.7z` / `*-portable.zip` | GitHub Release `v<项目版本>` |
| `*-setup.exe` / `*-setup.dmg` | 同一个 GitHub Release |
| `*-update.tar.zst` | R2 `releases/<文件名>` |
| `latest.json` | R2 `updates/stable/<平台>/latest.json` |

平台分别为 `windows-x64` 和 `macos-arm64`，各自的清单不会覆盖对方。

## 仓库配置

在 GitHub 仓库 Settings → Secrets and variables → Actions 添加：

- Secret `R2_ENDPOINT_URL`：`https://<ACCOUNT_ID>.r2.cloudflarestorage.com`（以 R2 控制台提供的 S3 API endpoint 为准）。
- Secret `R2_ACCESS_KEY_ID`：R2 S3 API Access Key ID。
- Secret `R2_SECRET_ACCESS_KEY`：对应的 Secret Access Key。
- Variable `R2_BUCKET`：目标 bucket 名称。

R2 凭证需要目标 bucket 的对象读写权限。GitHub 使用工作流自带的 `GITHUB_TOKEN`，发布 job 已声明 `contents: write`。

将 R2 bucket 通过自定义域名或已有 Worker 映射到 `https://darkeye.win/`，保持上述对象路径原样可公开访问。现有客户端读取 `https://darkeye.win/updates/stable/{target}/latest.json`，清单内下载地址为 `https://darkeye.win/releases/<文件名>`。上传到 R2 本身不会配置域名或 Worker。为 `latest.json` 禁用边缘缓存，避免旧缓存延迟更新。

## 触发

- 手动 Run workflow：勾选 `publish` 才会对外发布；不勾选时只构建并保存 Actions artifacts。
- 推送 `v*` 标签：自动构建并发布，标签必须与 `CMakeLists.txt` 的版本完全一致，例如 `v1.2.5`。
- 手动发布会使用项目版本创建标签，指向本次构建的 commit。

先校验两个平台全部文件、更新地址和 SHA256，再上传四个发行文件到 GitHub 草稿 Release。两个更新包均上传完成后，才更新各平台清单，最后公开 GitHub Release。失败时草稿保留，可重试；已经公开的同版本 Release 会被拒绝覆盖。两个平台的 R2 清单无法跨对象原子更新，部分失败时可重试工作流。

本地检查下载后的 artifacts（保留 `Darkeye-windows-x64/` 和 `Darkeye-macos-arm64/` 两个子目录）：

```bash
python3 tools/publish_release.py --artifacts artifacts --dry-run
```

参考：[Cloudflare R2 boto3](https://developers.cloudflare.com/r2/examples/aws/boto3/)、[GitHub CLI release create](https://cli.github.com/manual/gh_release_create)。
