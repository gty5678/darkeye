# 独立 QRhi DVD Demo

本目录按 `docs/darkeye-qrhi3d-implementation-plan.md` 实现独立原型。2026-10-23 起，主程序书架已复用同一 QRhi 场景和 GLB，见 [书架接入记录](../../docs/qrhi3d-shelf-integration.md)。本目录仍提供独立调试入口。

两个启动目标：

默认 DVD 模型来自 `libs/dvd/dvd.glb`，通过 Qt 资源嵌入两个 demo，替换文件后重新构建即可更新。原 `assets/dvd-case.glb` 不再用于 demo。加载时只取根节点 `back` 的子树与 `disc`，忽略文件中的第二套盒子、书架和辅助平面；`pic` 在内存中映射为可替换封面的 `cover`，保留其余材质属性。默认选择“模型原材质”，不覆盖 Blender 的透明材质。文件内动画仍不播放，开合由软件交互控制。`KHR_materials_clearcoat`、`KHR_materials_specular` 当前不支持，相关细节不会与 Blender 完全一致。

2026-10-23 更新：当前 GLB 已精简为 `back → spine → front` 与独立 `disc`，共 4 个节点、4 个网格、5 个材质、1 段动画，无内嵌贴图，不再包含额外盒子和书架。文件大小 139028 字节，SHA256 为 `243ba8fb1cbd7f5e89795b2a848fe754d494a3d754f7e77160b29452c0a5ae91`。已重新构建两个 demo，D3D11、4× MSAA 下 1000 件 DVD 的选中、开盒、截图与归位检查通过；日志和截图位于 `build/dvd-new-export-*`。

| 目标 | 用途 |
| --- | --- |
| `darkeye_dvd_scene_demo` | 完整 DVD 书架：真实盒模型、封面、选择/聚焦/开合、虚拟化和统计 |
| `darkeye_render3d_demo` | 渲染诊断：空场景、三角形、立方体、DVD GLB、MSAA 和资源重建 |

## 在 Visual Studio 中调试

用 VS 2022 打开仓库文件夹，选择 `windows-msvc-release`（构建 preset 为 `release`）。普通 Debug/Release 配置默认包含 graph demo、QRhi 3D demos 和 component gallery，无需开启测试。重新生成 CMake 缓存并构建后，在启动项下拉框选择 **`darkeye_dvd_scene_demo.exe`** 或 **`darkeye_render3d_demo.exe`**，按 F5。

程序位于 `build/windows-msvc-release/darkeye_dvd_scene_demo.exe`，源码入口为本目录 `dvd_demo.cpp`。若启动项仍旧，使用 VS 的“项目 → 配置缓存”重新配置；确认打开的是本仓库根目录。也可在 CMake 目标视图右键该 target，选择“设为启动项”。不要切到主程序 target。

Release 有编译优化，单步和局部变量可能不完整。完整断点调试可在 VS 单独打开本目录，使用下面的 Debug 构建，或在仓库 Debug 配置中开启同一选项。

## 仅构建 Demo

在 VS 2022 **x64 开发者 PowerShell** 中，从仓库根目录执行（Qt 路径按本机调整）：

```powershell
cmake -S apps/render3d_demo -B build/render3d-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 `
  -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build build/render3d-debug --parallel 4
./build/render3d-debug/darkeye_dvd_scene_demo.exe
ctest --test-dir build/render3d-debug --output-on-failure
```

Release 把目录改为 `build/render3d-release`、构建类型改为 `Release`。`Embedded` 使用 `/Z7` 保留调试信息，避免共享编译 PDB 服务的环境错误。此独立入口只需 Qt 6.12.0 Core/Gui/Widgets/GuiPrivate/ShaderTools，测试另需 Test；不需 vcpkg、数据库、Quick 或 QML。`-DBUILD_TESTING=OFF` 可关闭测试。Windows 自动运行 `windeployqt`，模型及 qsb 已嵌入程序，无需复制原始 shader。

完整仓库默认启用 `DARKEYE_BUILD_RENDER3D_DEMO` 与 `DARKEYE_BUILD_GRAPH_VIEW_DEMO`；需要缩减开发目标时可以设置为 OFF。`DARKEYE_BUILD_TESTS=ON` 也会包含这些 demo 以便运行冒烟测试。渲染库由主程序书架复用。

## 操作与输入数据

- 数量切换：0/1/10/100/500/1000；最多保留 60 个书架实例，另保留正在交互的对象。
- 单击选择，双击聚焦；再次单击或“打开/关闭”切换开盒。Esc/“返回”先关盒，在书架外转正并对齐槽位，再直线插回。
- 右键拖动 Orbit，中键拖动 Pan，Ctrl+滚轮 Zoom；普通滚轮横移书架并加速关盒。方向键移动。
- 序号框定位并聚焦作品；HDR 背景、IBL、AO、方向光阴影、色调映射均可独立开关，曝光范围 10%–400%；“环境旋转”滑条/角度框支持 0–360°，HDR 背景与 IBL 反射同步绕竖轴旋转。关闭 HDR 背景后可比较明暗纯色主题；封面预算、隐藏恢复、移动窗口与截图用于回归。
- 封面缺失/损坏会显示占位并输出路径警告。后台解码使用两个工作线程，普通封面 256、选中封面 512 像素；选中请求优先。
- 统计展示 D3D11、实际像素尺寸/DPR、CPU 提交时间、Draw Calls、共享几何和纹理缓存。静止、隐藏或最小化时停止主动动画重绘。

支持 `--items path/to/items.json` 或面板加载 JSON；相对封面路径以 JSON 文件夹为基准，支持中文路径。数据契约示例：

```json
{"items":[
  {"stableId":"dvd-001","coverPath":"covers/1.png","title":"示例作品","displayOrder":0,"selected":false}
]}
```

ID 必须非空且唯一；按 `displayOrder` 稳定排序，事件回传稳定 ID。公开 API 位于 `libs/dvd_scene/include/dvd_scene/DvdSceneWidget.h`：`setItems/updateItem/focusItem` 与选择/点击信号可供未来业务适配。当前不读取 SQL。

## 自动回放与性能记录

```powershell
./build/render3d-release/darkeye_dvd_scene_demo.exe --count 1000 --smoke-test
./build/render3d-release/darkeye_dvd_scene_demo.exe --items build/qrhi3d-baseline/items.json `
  --screenshot build/dvd-open.png --screenshot-closed build/dvd-closed.png
./build/render3d-release/darkeye_dvd_scene_demo.exe --items build/qrhi3d-baseline/items.json `
  --benchmark build/qrhi3d-new-benchmark.json
./build/render3d-release/darkeye_render3d_demo.exe --scene dvd --smoke-test
```

Smoke 会聚焦/开盒、读回 framebuffer、关盒返回并校验错误、退出；0 成功，2 错误，3 保存失败，4 超时。Benchmark 使用 1/10/100/500/1000 子集、1280×720、4x MSAA、相同封面数据、60 件驻留窗口；记录 CPU、帧提交次数、绘制数和缓存估算。`--items` 用于 benchmark 时至少需 1000 件。

旧版 QML 对照录制器及离线 Qt 网格导出工具已于 2026-10-25 删除。历史对比记录见 docs/qrhi3d-visual-comparison.md；当前光照和测量见 docs/qrhi3d-lighting.md。

## 架构、材质与资源生命周期

透明塑料增量：当前默认使用用户 GLB 的原材质，侧栏仍可选择透明盒壳、薄片或“透明盒壳＋外膜预览”。盒壳参数预设仅覆盖名为 `shell` 的材质；当前导出的 `trans` 保留原参数。外膜预览生成偏移薄面和细纹法线，原 GLB 不变。支持 glTF transmission / IOR / volume 及贴图，以及按物体深度排序的多层屏幕空间透射；前层可采样先前合成的透明层和 Alpha Blend。默认最多 8 次合成，可通过侧栏或 `--transmission-passes 1..16` 调整，统计显示超限回退数。交叉透明几何、焦散、体散射和有色透射阴影仍未支持。完整限制与 Blender 参数见 [材质指南](../../docs/qrhi3d-materials.md)。

“载入 DVD GLB…”或 `--model path.glb` 可载入 Blender 导出的模型并保留其原材质；需要 `back/spine/front/disc` 节点。`--shell-material original|plastic|thin|film` 显式选择预览。更换模型/预览会重置开盒姿态。

`render3d` 负责 CPU Scene/Node/Transform、Camera、Mesh/Material/ModelAsset、cgltf 导入、动画和盒体拾取；`dvd_scene` 负责数据、布局、交互、异步封面与 Widget。Renderer 私有实现持有当前 QRhi 的 Buffer、Texture、Bindings、Pipeline；CPU 资产不持有 GPU 指针。实例共享几何，GPU 缓存按当前场景资产清理，封面有 CPU LRU 和 GPU 源纹理预算；预算不足时降低上传分辨率。

`initialize/render/releaseResources` 支持重建；QRhi 更换释放全部 GPU 资源，CPU 场景保留，普通 resize 不重传几何。Pipeline 按透明模式/双面/镜像绕序及 render pass/sample 数复用，透明背向排序且不写深度。不持续保留 Widget 的临时 render target 指针。

坐标为右手系、相机朝 -Z，通过 `clipSpaceCorrMatrix` 适配后端。glTF baseColor/emissive 纹理按 sRGB 解码，MR/normal/occlusion 数据纹理线性，材质因子线性；RGBA16F 中间目标进行光照和透明混合，显示 pass 编码回 sRGB。旧诊断顶点色显式从 sRGB 解码。已实现 GGX Metallic-Roughness、法线贴图、材质 AO、自发光、HDR 环境、漫反射 irradiance 与 GGX 预滤波反射 IBL、SSAO、方向光 PCF 阴影，以及可关闭的 ACES 拟合色调映射。AO 只调节间接光，阴影只调节方向光。DVD 材质拆分、Blender 参数和贴图约定见 [材质制作说明](../../docs/qrhi3d-materials.md)。

导入采用 cgltf v1.15（MIT），支持静态三角网格 GLB/外部 glTF、层级、多个 primitive、缺法线生成、切线、baseColor/MR/normal/occlusion/emissive 图、采样与透明模式，以及 KHR_materials_emissive_strength。明确拒绝不支持的 required extension/非静态网格；缺图警告并降级。限制文件/节点/顶点/解码图像大小，避免损坏资产造成无限分配。详见 loader 与 `third_party/cgltf/README.md`。

`assets/dvd-case.glb` 历史上由四个 Qt `.mesh` 离线导出，保留原层级、9 个材质子集、几何与法线，V 转换为 `1-v`；Qt ARGB 颜色转换为线性 glTF 因子。`dvd-case.provenance.json` 保存来源和哈希。原 Qt 网格和导出工具已删除；GLB 与来源哈希作为历史资产记录保留。仓库未提供原模型单独的上游许可证，导出不改变其授权状态。

## 验证和问题排查

CTest 共 11 项，Debug/Release 都运行 CPU 变换/相机/拾取/动画、glTF/错误资产、DPR 1/1.5 D3D11 framebuffer、DVD 交互和三个 smoke。覆盖鼠标点击/双击/拖动/滚轮、1000 件、封面替换/缺失/损坏、异步销毁、缓存预算/淘汰、空闲无重绘、4K/最小画布、主题、隐藏/最小化/跨窗口 reparent、MSAA/格式变更和重建。新增 HDR 解码/卷积、IBL 金属反射、曝光、AO 接触区、遮挡物移动后的投影及环境纹理重建测试。报告位于构建目录 `render3d_tests/*.txt`、`dvd_scene_tests/*.txt`。

`darkeye.render3d` 和 `darkeye.render3d.assets` 日志输出后端、尺寸、重建及资产错误；GPU 创建失败会红色清屏并输出错误。若 VS 修改头文件后行为与源码不符，检查 `ninja -t deps`；中文 MSVC showIncludes 探测乱码已在 `cmake/CompilerOptions.cmake` 定向修正，不能混用旧对象。Qt 升级后需重跑 GUI 测试。

仍需在实际多屏硬件验证跨屏 DPI/长时间驱动显存趋势。书架业务数据接入、正式场景替换和主窗口回归已在 2026-10-23 完成，见 [接入记录](../../docs/qrhi3d-shelf-integration.md)；发布安装包验证另行进行。



