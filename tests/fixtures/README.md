# Compatibility fixtures

本目录只存放由固定 Python 基线或等价空库生成的脱敏夹具，禁止放入用户真实
数据库、路径或私人记录。

- `schema/`：由初始化后的 SQLite 数据库 introspection 生成，包含完整表、
  字段、约束、索引、视图和触发器。
- `databases/`：空库、历史版本、脱敏典型库和故障库。
- `seeds/`：典型库的可审查虚构数据源。
- `golden/`：查询、写入、导入和 HTTP 对照输出；当前已加入核心查询首批样本。
