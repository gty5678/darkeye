# 封套制作器 Demo

```sh
cmake --preset macos-arm64-debug -DDARKEYE_BUILD_DEMOS=ON
cmake --build build/macos-arm64-debug --target darkeye_cover_maker_demo
./build/macos-arm64-debug/darkeye_cover_maker_demo
```

Windows 可用对应的预设启用 `DARKEYE_BUILD_DEMOS`，再构建同名目标并运行对应 `.exe`。

## 快速制作

1. 把图片文件拖到画布，或点击左上角图片图标导入；图片会等比适应，并可自由拖动、角点 / 滚轮缩放、直接裁剪。
2. 点击文字图标后直接输入，Enter 完成，Shift+Enter 换行；双击画布上的文字继续编辑。
3. 右侧直接调整字体、字号、加粗、旋转、颜色、横排 / 竖排和字间距。
4. 顶部“导出 PNG…”保存完整封套，参考线和选择框不会导出。

默认空白画布 **3200×2160**，辅助分区宽度 **1525 / 156 / 1519**，来自当前 DVD 模型。尺寸统一通过顶部“画布设置…”弹窗调整，确认生效，取消不改变构图；参考线与区域名默认开启，可整体关闭。

常用操作默认直接显示。右侧“更多”默认折叠，包含完整图层列表、显隐、锁定、复制、排序、详细对齐、加粗、旋转和文字效果。吸附自动工作，拖动时按 Alt 暂停。没有快速布局或预置素材。

撤销 / 重做支持按钮和系统快捷键；一次拖动、裁剪确认或批量导图作为一步历史，取消裁剪不留历史。裁剪模式 Enter 完成、Esc 取消，保留原图。

Demo 无条目侧栏、草稿列表、自动保存，也不显示“应用”。编辑内容仅在本次运行内保留；条目宿主仍可使用库的 `applyRequested` 接口。

视觉检查：`darkeye_cover_maker_demo --snapshot /absolute/path/preview.png`。
测试：`ctest --test-dir build/macos-arm64-debug -R '^darkeye_cover_maker_tests$' --output-on-failure`。

画布上方常驻“吸附”开关，默认开启，可随时关闭；按住 Alt 仍可临时暂停吸附。

横排文字属性提供“左右字间距”：0 为字体原始间距，正数拉开、负数收紧，单位为导出像素。横排与竖排间距独立保存，切换排列后保留设置。字形绘制、选框、居中、吸附和导出共用调整后的字体度量。

文字旋转为常用属性中的角度输入框，支持 -360° 至 360° 的任意角度（两位小数），正数顺时针、负数逆时针，输入实时更新，并支持撤销 / 重做。

## 超分服务

选中图片后，属性栏“高清增强…”固定进行 2 倍增强，处理中顶部可取消，完成后可撤销。位置、尺寸和裁剪保持不变。处理期间若修改文档，本次结果将丢弃，避免覆盖新编辑。

“更多 → 超分服务设置…”填写完整 POST 地址与令牌；默认 `http://127.0.0.1:8443/v1/upscale`。本机开发可填 `http://127.0.0.1:8443/v1/upscale`。也支持 `DARKEYE_SR_URL` / `DARKEYE_SR_TOKEN` 环境变量；自建 CA 用 `DARKEYE_SR_CA_FILE` 配置信任。URL 持久保存，令牌只在当前会话保留。

独立 Python 服务位于同级 `SRkit` 项目，使用 uv / FastAPI / Real-ESRGAN ncnn Vulkan / PyInstaller；编辑器不携带模型或 CUDA 依赖。图片将上传到配置的服务，返回 PNG 后替换原图。
