# DarkEye 采集 HTTP 接口

DarkEye 桌面端在 `http://127.0.0.1:56789` 接收浏览器扩展和本机工具的请求。DarkEye 发起爬取时，默认调用 `http://127.0.0.1:56790` 上的 Mosaic Bridge（`jav-server`）。

## DarkEye 提供的接口

POST 请求使用 `Content-Type: application/json`，请求体为 JSON 对象，最大 1 MiB。

| 方法与路径 | 请求 | 响应与行为 |
| --- | --- | --- |
| `GET /api/v1/health` | 无请求体 | 返回 `{"status":"ok","service":"DarkEye Server"}`，供扩展弹窗检测桌面端。 |
| `POST /api/v1/check_existence` | `items` 为番号字符串数组。 | 返回 `results` 对象，以原始番号字符串为键，布尔值表示本地库是否已收录。查询时去掉首尾空白并转为大写，已软删除的记录按未收录处理。 |
| `POST /api/v1/capture/one` | `content` 为非空番号字符串。 | 去掉首尾空白并交给采集队列，返回 `{"status":"success","message":"Data received"}`。回执表示已收到请求，爬取和入库由后台任务处理。 |
| `POST /api/v1/minnano-actress-capture` | 扩展发送 `data`、`url` 和 `context`。也可直接传资料对象。若 `context.actress_id` 为正数，须与当前编辑对象一致。 | 返回 `{"status":"success","message":"Data received"}`。匹配当前编辑对象时尝试填入女优编辑页，表单提交后才保存。 |
| `POST /api/v1/crawler-backlog-warning` | `count` 为积压标签数，`threshold` 默认 `13`，`browser` 默认 `firefox`。 | 达到阈值时弹出桌面提醒，返回 `{"status":"success","message":"notified"}`。低于阈值时返回 `{"status":"ignored","reason":"below_threshold"}`。 |
| `POST /api/v1/cloudflare-challenge-notify` | `site` 为站点名，`serial` 为关联番号，两者均可为空。 | 提醒用户在浏览器中完成验证，返回 `{"status":"success","message":"notified"}`。 |

错误响应为 `{"detail":"…"}`。无效 JSON 或请求体不是对象时返回 `400`，请求体过大时返回 `413`，必填字段缺失或类型不符时返回 `422`，查重数据库不可用或查询失败时返回 `500`。未注册的请求返回 `404`，`OPTIONS` 预检返回 `204`。

### 请求与响应示例

#### 健康检查

```http
GET /api/v1/health HTTP/1.1
Host: 127.0.0.1:56789
```

响应 `200 OK`

```json
{
  "status": "ok",
  "service": "DarkEye Server"
}
```

#### 批量查重

```http
POST /api/v1/check_existence HTTP/1.1
Host: 127.0.0.1:56789
Content-Type: application/json

{"items":["ABC-123","ABC-456"]}
```

响应 `200 OK`

```json
{
  "results": {
    "ABC-123": true,
    "ABC-456": false
  }
}
```

#### 提交单个作品

```http
POST /api/v1/capture/one HTTP/1.1
Host: 127.0.0.1:56789
Content-Type: application/json

{"content":"ABC-456"}
```

响应 `200 OK`

```json
{
  "status": "success",
  "message": "Data received"
}
```

省略 `content` 或传入空字符串时返回 `422 Unprocessable Entity`

```json
{
  "detail": "content is required"
}
```

#### 接收 Minnano 女优资料

示例假设桌面端正在编辑 ID 为 `42` 的女优。

```http
POST /api/v1/minnano-actress-capture HTTP/1.1
Host: 127.0.0.1:56789
Content-Type: application/json

{"context":{"actress_id":42},"url":"https://example.com/actress/123","data":{"日文名":"山田花子","假名":"やまだはなこ","身高":162,"头像地址":"https://example.com/portrait.jpg"}}
```

响应 `200 OK`

```json
{
  "status": "success",
  "message": "Data received"
}
```

#### 接收爬虫积压提醒

```http
POST /api/v1/crawler-backlog-warning HTTP/1.1
Host: 127.0.0.1:56789
Content-Type: application/json

{"count":14,"threshold":13,"browser":"chrome"}
```

响应 `200 OK`

```json
{
  "status": "success",
  "message": "notified"
}
```

如果改传 `count: 12`，则返回 `200 OK`

```json
{
  "status": "ignored",
  "reason": "below_threshold"
}
```

#### 接收 Cloudflare 验证提醒

```http
POST /api/v1/cloudflare-challenge-notify HTTP/1.1
Host: 127.0.0.1:56789
Content-Type: application/json

{"site":"JavDB","serial":"ABC-456"}
```

响应 `200 OK`

```json
{
  "status": "success",
  "message": "notified"
}
```

## DarkEye 调用的爬虫接口

下列路径以默认的 Mosaic Bridge 地址 `http://127.0.0.1:56790` 为基准。设置项对应 DarkEye 配置文件中的 `Crawler` 组。

| 方法与路径 | 设置项 | 参数与返回 |
| --- | --- | --- |
| `GET /api/v1/work/{serial_number}` | `Crawler/WorkApiBaseUrl` | DarkEye 将番号作为路径段追加到配置的 URL。成功响应包含 `ok: true`、`serial_number`、合并后的作品对象 `data` 和各站结果 `per_site`。DarkEye 使用非空 `data` 处理作品入库。 |
| `GET /api/v1/actress/{actress_jp_name}` | `Crawler/ActressApiBaseUrl` | 路径段为 URL 编码后的日文名，可附加 `minnano_url` 查询参数。成功响应包含 `ok: true`、`actress_jp_name` 和资料对象 `data`。 |
| `GET /api/v1/top-actresses` | `Crawler/TopActressesApiUrl` | 无参数。成功响应包含 `ok: true` 和日文名数组 `names`。 |
| `POST /api/v1/image` | `Crawler/CoverFetchApiUrl` | JSON 请求体为 `{"url":"https://example.com/cover.jpg"}`。成功响应包含 `success: true` 和 Base64 编码的 `image`，DarkEye 将图片保存为 JPEG。 |

作品、女优和热门榜单接口可能在 HTTP `200` 时返回 `ok: false` 和 `error`。Mosaic Bridge 未连上扩展时，这三类请求返回 `503`，等待结果超时时返回 `504`。有效图片请求在下载失败时返回 `success: false` 和错误消息。

### 请求与响应示例

#### 爬取作品

响应中的 `data` 示例只列出部分作品字段。

```http
GET /api/v1/work/ABC-456 HTTP/1.1
Host: 127.0.0.1:56790
```

响应 `200 OK`

```json
{
  "ok": true,
  "serial_number": "ABC-456",
  "data": {
    "serial_number": "ABC-456",
    "jp_title": "作品タイトル",
    "release_date": "2025-04-15",
    "cover_url_list": [
      "https://example.com/cover.jpg"
    ]
  },
  "per_site": {}
}
```

扩展未连接时返回 `503 Service Unavailable`

```json
{
  "detail": "no browser extension connected (SSE)"
}
```

等待扩展回传超时时返回 `504 Gateway Timeout`

```json
{
  "detail": "work merge timed out waiting for browser extension"
}
```

#### 爬取女优资料

路径中的日文名和查询参数中的链接均做 URL 编码。此处使用占位链接，实际请求应传对应的 Minnano 详情页 URL，也可以省略此参数。

```http
GET /api/v1/actress/%E5%B1%B1%E7%94%B0%E8%8A%B1%E5%AD%90?minnano_url=https%3A%2F%2Fexample.com%2Factress%2F123 HTTP/1.1
Host: 127.0.0.1:56790
```

响应 `200 OK`

```json
{
  "ok": true,
  "actress_jp_name": "山田花子",
  "data": {
    "日文名": "山田花子",
    "假名": "やまだはなこ",
    "身高": 162,
    "头像地址": "https://example.com/portrait.jpg"
  }
}
```

#### 爬取热门女优榜单

```http
GET /api/v1/top-actresses HTTP/1.1
Host: 127.0.0.1:56790
```

响应 `200 OK`

```json
{
  "ok": true,
  "names": [
    "山田花子",
    "佐藤春香"
  ]
}
```

#### 下载图片

实际请求应传可访问的 HTTP 或 HTTPS 图片链接。

```http
POST /api/v1/image HTTP/1.1
Host: 127.0.0.1:56790
Content-Type: application/json

{"url":"https://example.com/cover.jpg"}
```

响应（`image` 内容已截短） `200 OK`

```json
{
  "success": true,
  "image": "/9j/4AAQSkZJRgABAQ..."
}
```

等待扩展回传超时时返回 `200 OK`

```json
{
  "success": false,
  "image": null,
  "message": "未在时限内返回图片"
}
```

## 浏览器手动采集链路

1. Chrome 或 Firefox 扩展直接向 DarkEye 的 `56789` 端口查重。用户点击作品上的「+ 采集」后，扩展提交番号，收到回执时显示「已提交」。
2. DarkEye 的 `CrawlerScheduler` 从队列取出番号，调用作品爬虫接口。使用默认配置时，Mosaic Bridge 通过 `GET /events` 的 SSE 连接将任务派给浏览器扩展。
3. 扩展把爬取结果回传给 Mosaic Bridge，由它返回给等待中的 DarkEye 请求。DarkEye 处理作品和图片入库，之后查重才会显示「已收录」。

扩展发出的 Minnano 手动采集、积压提醒和 Cloudflare 提醒仍走 Mosaic Bridge 的转发路径。使用默认配置时，手动提交由 DarkEye 接收，后续爬取需要 Mosaic Bridge 和已连接的浏览器扩展。
