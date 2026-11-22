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
移动、拆分、布局重建使用 `takeContent()`，不会触发关闭检查。
关闭后默认采用 `ClosePolicy::DeleteLater`，在当前事件结束后销毁页面，避免同步释放重页面卡住关闭操作。
需要保留复用时，使用 `ContentConfig::setClosePolicy(ClosePolicy::KeepAlive)`；
直接调用 `PaneWidget::addContent()` 时可通过最后一个参数设置该策略。
`KeepAlive` 会隐藏页面并解除父对象关系，由调用方负责重新放入窗格或销毁，关闭最后一个标签导致窗格移除时页面也能保留。
移动和失败回滚会保留关闭策略，不会销毁页面。关闭策略由内容工厂在恢复布局时重新指定。
布局加载、重置和应用退出仍由调用方管理，不属于单个标签的关闭请求。

布局恢复会先完成所有内容工厂调用，再转移工厂返回的现有页面并替换布局。
失败时原窗格、标签顺序、当前标签和活动窗格保持原样；工厂新建且无父对象的页面由加载过程接管并在失败时回收。
工厂返回 `std::nullopt` 仍表示跳过该内容；同一页面不能用于多个待恢复标签。

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
