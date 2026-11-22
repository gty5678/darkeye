# Darkeye UI 组件库 Demo

该程序对应 Python 的 `python -m darkeye_ui.demo`，只演示
`darkeye::darkeye_ui` 公共组件，不依赖数据库或 Darkeye 业务页面。

```powershell
cmake --build --preset debug-tests --target darkeye_component_gallery
build/windows-msvc-debug-tests/darkeye_component_gallery.exe
```

批量生成当前主题或全部七套主题的页面截图：

```powershell
darkeye_component_gallery.exe --snapshot-dir <输出目录>
darkeye_component_gallery.exe --snapshot-dir <输出目录> --all-themes
```
