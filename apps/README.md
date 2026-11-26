# 应用与独立演示程序

普通构建默认生成主程序。Demo 统一通过 `DARKEYE_BUILD_DEMOS=ON` 开启。

| 源码目录 | 可执行目标 | 用途 |
| --- | --- | --- |
| `desktop/` | `Darkeye` | 主程序 |
| `cover_maker_demo/` | `darkeye_cover_maker_demo` | 封底、书脊、封面独立编辑与整张封套合成 |
| `graph_view_demo/` | `darkeye_graph_view_demo` | 力导向图、GPU 布局与图内性能表 |
| `render3d_demo/` | `darkeye_render3d_demo` | QRhi 3D 渲染诊断 |
| `render3d_demo/` | `darkeye_dvd_scene_demo` | 完整 DVD 书架场景 |
| `component_gallery/` | `darkeye_component_gallery` | UI 组件与主题展示 |

例如，在 VS 2022 开发者终端中启用 Demo，再单独构建关系图目标。

```powershell
cmake --preset windows-msvc-release -DDARKEYE_BUILD_DEMOS=ON
cmake --build --preset release --target darkeye_graph_view_demo
```

Demo 可执行文件输出到构建目录根目录，可直接运行或在 Visual Studio 的启动项中选择。

同时开启 `DARKEYE_BUILD_TESTS` 和 `DARKEYE_BUILD_DEMOS` 后，CTest 会注册 Demo 相关测试。

[MdEdit 完整版](mdedit/README.md)位于 `apps/mdedit/`，使用独立的 CMake 构建目录和依赖清单。
