# Compatibility fixtures

数据库夹具由固定 Python 基线或等价空库生成，禁止放入用户真实数据库、路径或私人记录。

- `schema/`：由初始化后的 SQLite 数据库 introspection 生成，包含完整表、
  字段、约束、索引、视图和触发器。
- `schema/legacy/`：用于构造旧版公有库和私有库的基线 SQL。
- `databases/`：空库、历史版本、脱敏典型库和故障库。
- `seeds/`：典型库的可审查虚构数据源。
- `golden/`：查询、写入、导入和 HTTP 对照输出；当前已加入核心查询首批样本。
- `render3d/`：由原 Qt 网格离线导出的 DVD GLB，用于验证 glTF 加载、材质和书架交互。来源、哈希和授权说明见同目录的 `dvd-case.provenance.json`。
