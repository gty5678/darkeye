# 力导向图 Demo

目标名为 `darkeye_graph_view_demo`，普通 Debug/Release 构建默认包含，不依赖测试开关。完整构建入口见 [apps 说明](../README.md)。

```powershell
cmake --build --preset release --target darkeye_graph_view_demo
./build/windows-msvc-release/darkeye_graph_view_demo.exe --backend gpu --nodes 10000
```

`--backend` 支持 `auto`、`gpu`、`cpu`；`--nodes` 设置初始节点数，运行时也可在侧栏切换。图内右键勾选“性能表”，即可在左上角显示当前图的性能指标、后端选择及 GPU 阶段计时开关。

侧栏“测试操作”集中提供适配视图、重启、暂停、继续、加点、编辑点、减点、加边、减边和性能表入口。编辑只影响当前 demo 的内存图；重新生成会清除编辑。将节点数量设为 2000 可生成测试图。正式软件的关系图保留布局、显示设置和右键性能统计，不提供测试操作或测试图入口。

侧栏提供“推荐组合”“初始位置”“关联结构”“目标平均度”和“随机种子”。选择参数后点击“应用 / 重置布局”；“换种子并生成”使用下一个种子。位置与连边使用独立随机流，因此只改关联结构会保留相同初始位置，只改位置分布会保留相同连边。侧栏显示已应用图的实际边数和平均度；图内性能表仍独立存在。

| 推荐组合 / `--preset` | 位置与关联结构 | 主要测试目的 |
| --- | --- | --- |
| 基线 / `baseline` | 均匀圆盘＋均匀随机边 | 比较节点规模和一般吞吐 |
| 社群 / `communities` | 八团＋团内连接、少量桥接 | 社群分离和布局质量 |
| 枢纽 / `hubs` | 均匀圆盘＋优先连接网络 | 高度节点、连接力和邻居高亮 |
| 规则 / `lattice` | 规则格点＋二维网格边 | 对称性和收敛稳定性 |
| 星形 / `star` | 圆环＋单中心连接 | 极端高度节点和长边 |
| 孤岛 / `islands` | 聚团＋最多八个独立分量 | 多分量排布和分离 |
| 重叠 / `overlap` | 原点重叠＋随机边 | 重复空间键、碰撞退化与数值稳定性；先用 400–2000 点 |

初始位置还可选中心高斯，关联结构还可选闭合环或无边。结构模式为网格、环、星形、无边时，平均度由结构决定，目标平均度输入框会禁用；社群与优先连接模式的实际平均度与目标近似，数值以侧栏显示为准。所有边均为无自环、无重复的无向边。

```powershell
# 相同种子对比 GPU / CPU
./build/windows-msvc-release/darkeye_graph_view_demo.exe --backend gpu --nodes 10000 --preset hubs --seed 42
./build/windows-msvc-release/darkeye_graph_view_demo.exe --backend cpu --nodes 10000 --preset hubs --seed 42
# 独立组合位置与关联结构
./build/windows-msvc-release/darkeye_graph_view_demo.exe --nodes 2000 --positions gaussian --topology random --degree 8 --seed 42
```

`--positions`：`disk / gaussian / clusters / grid / ring / overlap`；`--topology`：`random / communities / scale-free / grid / ring / star / disconnected / none`。节点数范围 1–100000，目标平均度 1–16，种子 0–2147483647。推荐先用 400 或 2000 点看布局，再用 10000、100000 点比较性能；改变平均度时同时记录实际边数。

`--smoke-test` 用于首帧渲染检查，`--text-motion-smoke` 检查运动中的视口稳定性；相关 CTest 注册保留在 `tests/CMakeLists.txt`。
