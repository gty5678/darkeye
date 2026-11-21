# 浏览器式停靠工作区

桌面主窗口采用 MyADS WorkspaceWidget，按 Ctrl/Cmd+O 搜索实体，按 Ctrl/Cmd+P 打开命令面板，按 Ctrl/Cmd+, 直达设置。
原来的 Sidebar / mainPages 单页切换容器已移除。

## 使用

以下为默认快捷键，Ctrl/Cmd 表示 Windows 和 Linux 使用 Ctrl，macOS 使用 Cmd。工作区快捷键可在「设置」→「快捷键」中修改。

- Ctrl/Cmd+O 支持按姓名、别名、番号或标题搜索，也能查找标签、片商、厂牌、系列和导演。选择男优、片商、厂牌、系列或导演时，打开 DVD 书架并应用对应筛选。搜索框为空时显示最近打开的最多 9 个实体。“查看全部搜索结果”会打开独立搜索标签。
- Ctrl/Cmd+P 无需输入即可浏览所有页面命令，最近打开的页面排在前面。输入“作品库”“统计”或“标签管理”等名称可直接筛选。
- 输入「批量」可查找维护命令。选择命令后在当前工作区执行，重新打开命令面板可查看任务状态。
- 方向键选择结果，Enter 确认，Escape 或焦点离开弹窗时关闭。
- 作品详情在 3D 书架中定位并展开。人物、编辑器和剧照按实体 ID 创建独立标签，同一路由重复打开会定位已有标签。
- 在命令面板打开「作品过滤器」标签页。筛选条件和排序同时应用到作品页和 DVD 书架。
- 拖动标签到内容区边缘分屏，拖到另一个标签组中合并；右键标签提供批量关闭及移动操作。
- 按 Ctrl/Cmd+Alt+→ 或 Ctrl/Cmd+Alt+← 按工作区布局顺序循环切换到下一个或上一个窗格。macOS 上的 Alt 键为 Option。
- Ctrl/Cmd+W 关闭当前标签。当前窗格内循环切换标签页时，macOS 使用 `Cmd+Shift+]` 和 `Cmd+Shift+[`，Windows 和 Linux 使用 `Ctrl+Tab` 和 `Ctrl+Shift+Tab`，分别切到下一个和上一个标签页。
- 退出时保存布局、标签顺序和活动页到配置目录 workspace-v2.json，下次启动恢复。没有可恢复的标签时，或关闭最后一个标签后，空工作区会提示两个快捷键。
- 标签管理、片商管理、厂牌管理、系列管理、汇总查询、个人记录、软删除和回收站均可通过 Ctrl/Cmd+P 打开。
- 标签切换与拖动保留页面的筛选和滚动状态。
- 女演员列表成为活动页时，局部关系图显示符合列表筛选的女演员及其关联作品，并随筛选更新。
- 在 Ctrl/Cmd+P 中打开「时间轴」，可拖动标签分屏。焦点切到女优详情或编辑页时，时间轴显示该女优参与的作品。焦点切到作品库或 DVD 书架时，时间轴随筛选显示完整结果。操作时间轴时会保留最近关联的页面，点击作品标记可在书架打开作品。
- 人物和作品编辑页关闭时支持保存、放弃和取消；应用退出也会先执行关闭检查。

## 路由与生命周期

`MainWindow::ensurePage` 按路由缓存 QPointer；`navigateTo` 将页面放入活动窗格或定位已有内容。
路由形式示例：

| 路由 | 页面 |
| --- | --- |
| actress / actor / mutiwork | 人物库和作品库 |
| information / chart | 可独立打开、上下滚动的信息面板和统计 |
| actress_detail/ID | 独立女优详情 |
| actor_works/ID | 在 DVD 书架筛选男优的作品 |
| work_detail/ID / work_edit/ID | 在 3D 书架定位作品 / 独立作品编辑 |
| modify_actress/ID / modify_actor/ID | 独立人物编辑 |
| graph / local_graph | 全局关系图和随焦点更新的局部关系图 |
| timeline | 随工作区焦点更新的作品时间轴 |
| fanart_work/ID | 作品剧照 |
| tag/ID | 标签作品视图 |
| maker/ID / label/ID / series/ID / director/ENCODED_NAME | 在 DVD 书架筛选作品 |
| search/ENCODED_QUERY | 完整搜索结果，支持继续加载 |
| manage_* | 拆分后的管理功能 |

PaneWidget::contentClosed 只在实际关闭成功后发出，经 WorkspaceWidget 转发。
主窗口立即移除缓存，避免在 deleteLater 执行前重新打开并复用待销毁页面。
移动、分屏、布局恢复不会发出此关闭信号。

持久化恢复使用实体 ID，不保存编辑器未提交的表单。列表筛选状态随打开的页面实例保留；重启恢复布局时，列表重新加载默认筛选。
