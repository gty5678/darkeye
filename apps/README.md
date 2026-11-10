# 应用与独立演示程序

以下目标属于日常开发入口，普通构建默认提供，无需开启 `DARKEYE_BUILD_TESTS`。

| 源码目录 | 可执行目标 | 用途 |
| --- | --- | --- |
| `cover_maker_demo/` | `darkeye_cover_maker_demo` | 封底、书脊、封面独立编辑与整张封套合成 |
| `desktop/` | `Darkeye` | 主程序 |
| `graph_view_demo/` | `darkeye_graph_view_demo` | 力导向图、GPU 布局与图内性能表 |
| `render3d_demo/` | `darkeye_render3d_demo` | QRhi 3D 渲染诊断 |
| `render3d_demo/` | `darkeye_dvd_scene_demo` | 完整 DVD 书架场景 |
| `component_gallery/` | `darkeye_component_gallery` | UI 组件与主题展示 |

在 VS 2022 开发者终端配置、构建：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release --target darkeye_graph_view_demo darkeye_render3d_demo darkeye_dvd_scene_demo darkeye_component_gallery
```

四个开发程序均输出到 `build/windows-msvc-release/` 根目录，可以直接运行，也可以在 Visual Studio 的启动项中选择；不必在 `tests` 或 `*-tests` 构建目录中查找。普通 Debug 对应 `windows-msvc-debug` / `debug`，输出到 `build/windows-msvc-debug/`。

graph 与 3D demo 分别由 `DARKEYE_BUILD_GRAPH_VIEW_DEMO`、`DARKEYE_BUILD_RENDER3D_DEMO` 控制，默认 ON；gallery 始终包含。测试构建也提供这些目标，自动化测试和冒烟测试注册仍位于 `tests/`。这些开发程序部署运行依赖，但不加入主程序安装包。

封面 Demo 由 `DARKEYE_BUILD_COVER_MAKER_DEMO` 控制，默认 ON。详见 [cover_maker_demo/README.md](cover_maker_demo/README.md)。
