# MyADS C++ 高级停靠区

这是 Python `ui/myads` 的 Qt/C++ 迁移版本，对外目标为 `darkeye::docking`。

## 已迁移能力

- `LayoutTree` 的横向、纵向、嵌套与根级拆分，以及拆分比例归一化。
- `PaneWidget` 的稳定内容 ID、标签顺序、活动标签、图标、关闭权限和内容转移。
- 标签在同一窗格内排序，以及拖到中央合并、四边拆分、工作区四边中段根级拆分。
- 跨组拖到标签栏时显示主题色插入线：指向标签前半部插到其前面，后半部插到其后面；标签栏空白处追加到末尾。标签栏优先于边缘拆分，拖到内容区仍按原来的合并／拆分规则处理。
- 拖放区域预览和独立 `DockTheme`，不会修改应用全局样式表。
- `schema_version=1` 布局、内容描述、窗格元数据和活动标签的原子保存与恢复。
- 加载前完整校验；在隐藏工作区准备布局和内容，工厂失败时保留原工作区并回收临时页面。

## 页面关闭检查

关闭按钮、右键“关闭其他 / 关闭右侧”和 `PaneWidget::removeContent()` 都会先检查
`closeable`，再向标签的内容 QWidget 同步发送 `QCloseEvent`。页面可以重写
`closeEvent(QCloseEvent *)`，在其中询问是否保存：保存成功或放弃修改时调用
`event->accept()`；取消或保存失败时调用 `event->ignore()`，标签和页面保持原样。
未重写的普通 QWidget 默认接受关闭。

批量关闭按标签顺序执行，遇到拒绝即停止；此前已关闭的标签不会恢复。
同一标签正在处理关闭请求时，重复关闭请求返回 false。
移动和拆分会转移原页面，保留页面状态和关闭策略。
关闭后默认采用 `ClosePolicy::DeleteLater`，在当前事件结束后销毁页面，避免同步释放重页面卡住关闭操作。
需要保留复用时，将 `ContentConfig` 的 `closePolicy` 设为 `ClosePolicy::KeepAlive`；
直接调用 `PaneWidget::addContent()` 时可通过最后一个参数设置该策略。
`KeepAlive` 会隐藏页面并解除父对象关系，由调用方负责重新放入窗格或销毁，关闭最后一个标签导致窗格移除时页面也能保留。
恢复布局和重置会先向所有即将移除的页面发送 `QCloseEvent`，包括隐藏侧栏中的页面。
只要有一个页面拒绝关闭，整个布局变更就会取消，原标签保持打开。
恢复布局时，内容工厂返回的现有页面会直接转移。工厂为目标标签指定关闭策略，同一页面只能使用一次。
工厂可以返回 `std::nullopt` 跳过内容。恢复失败时，工作区回收工厂返回的无父对象 `DeleteLater` 页面，
无父对象的 `KeepAlive` 页面由调用方持有。

主窗口退出使用 `requestWindowClose()` 检查所有停靠页面，检查通过后保存当前布局。
`closeable` 控制单独关闭标签的权限。布局替换、重置和主窗口退出由页面通过 `QCloseEvent` 决定是否允许。

## 测试程序

构建 Release 演示程序：

```powershell
cmake --build --preset release --target darkeye_myads_demo
```

输出为 `build/windows-msvc/Release/darkeye_myads_demo.exe`。工具栏可以添加标签、
拆分、保存、恢复、重置和切换主题；也可以直接拖动标签测试五方向及根级停靠。

自动验证：

```powershell
ctest --test-dir build/windows-msvc -C Release -R darkeye_myads --output-on-failure
```
