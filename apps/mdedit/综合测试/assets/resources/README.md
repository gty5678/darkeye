# 综合测试资源夹具

由综合测试.md 第 26 节引用，architecture 测试逐一检查这些引用的资源加载结果。

- images/：仅保留 PNG、JPG、JPEG、BMP、GIF、WebP、TIF、TIFF 夹具；SVG 使用 ../test-card.svg。GIF 只测试静态帧。
- sample.pdf：两页 PDF，第一页紫色、第二页绿色，用于翻页测试。
- text/：所有已声明支持的文本扩展名，以及 BOM 编码、空文件、长行和多行摘要样例。
- errors/：故意损坏、不可解码及二进制文本；这里的资源预期加载失败。缺失文件故意不提供。

CSV 继续使用 ../tables/材料统计.csv。请在副本中进行写入及保存测试。
