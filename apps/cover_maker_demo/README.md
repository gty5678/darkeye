# 完整封套制作器 Demo

```sh
cmake --build build/macos-arm64-debug --target darkeye_cover_maker_demo
./build/macos-arm64-debug/darkeye_cover_maker_demo
```

Windows 构建相同目标，运行构建目录中的 `darkeye_cover_maker_demo.exe`。
`DARKEYE_BUILD_COVER_MAKER_DEMO` 默认 ON；开发程序不加入主程序安装包。

1. 左侧选择示例条目。
2. 上方分别设置封底宽、书脊宽、封面宽和共同高度，单位为像素；默认匹配现有 DVD 模型贴图。
3. 切换“封底 / 书脊 / 封面”编辑页，分别添加多张图片、拖动位置、滚轮或角点等比缩放，点击“裁剪当前图片…”框选保留范围，设置底色，并添加多段文字。点击文字后拖动位置，右侧修改内容、字体、字号、颜色、加粗和方向；选不到的文字可通过“文字图层”下拉框选择。书脊新文字默认沿窄条排列。
4. 下方实时预览按“封底 | 书脊 | 封面”合成的整张封套。
5. 点击“合成并导出完整封套 PNG…”或“应用完整封套到当前条目”。左侧更新已应用封套缩略图。

默认合成尺寸为 **3200×2160**，三个区域的宽度为 **1525 / 156 / 1519**，根据当前 `libs/dvd/dvd.glb` 的 UV 分界与 `resources/maps/0.png` 的画布比例设置，详见库 README。

“应用”将完整 PNG 原子保存到 `QStandardPaths::AppDataLocation/sleeves/DEMO-00N.png`；重启后仍能查看已应用封套。三个区域的编辑草稿只在当前进程内保留，切换条目不丢失，重启恢复默认示例构图。此演示不读取或修改 DarkEye 的正式数据库。

视觉检查：`darkeye_cover_maker_demo --snapshot /absolute/path/preview.png`。
测试：`ctest --test-dir build/macos-arm64-debug -R '^darkeye_cover_maker_tests$' --output-on-failure`。

图片也可通过图层下拉框选取，并调整遮挡顺序。“完整原图适应画布”可恢复裁剪前的原图并重新居中。

文字属性中的“文字排列”支持横排与正立竖排。竖排中按回车可换到左侧新列，切换排列后仍可拖动位置、修改字体字号并单独设置旋转。
