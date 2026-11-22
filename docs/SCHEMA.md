# DarkEye 数据库 Schema

采用sqlite，主要是零配置。

设计的ERD图片见excalidraw，整个数据库的架构就是那样子，还是非常的完善的，但是有很多的东西信息非常的难获取


使用SQLite来存储，文本使用utf8来处理
从excel导出的时候要导utf8
SQLite要设置utf8
因为涉及到日文的问题



状态分类
- 主动(身心主动)
- 半推半就(身心平常，但是也能接受做爱)
- 反抗(身心都强烈抗拒)
- 沉伦(心理上的放纵，但是身体要做爱，背德感，表现出享受与放纵，没有羞耻或抵抗，前置条件是被动)，
- 羞耻(心理上的抗拒 + 快感中混杂着道德负担或社交焦虑，动作含蓄遮掩身体、夹腿、蜷缩，被强制，脸红，前置条件是被动)
- 受虐快感(高强度，受虐但是渴望做爱，前置条件是被动)
- 冷漠(做爱无表情无状态，前置条件是被动)



> 基于 `resources/sql/initTABLE.sql`（公共库）与 `resources/sql/initPrivateTable.sql`（私有库）整理。

---

## 一、概览

| 数据库 | 文件 | 用途 |
|--------|------|------|
| **public.db** | initTABLE.sql | 作品、女优、男优、标签、制作商等公共元数据 |
| **private.db** | initPrivateTable.sql | 收藏、撸管、做爱、晨勃等个人行为数据 |

**注意**：`tag` 表依赖 `tag_type`，`prefix_maker_relation` 依赖 `maker`。`tag_type` 与 `maker` 可能在种子数据或迁移脚本中创建。

---

## 二、公共库 (public.db)

### 2.1 work — 作品表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| work_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| serial_number | TEXT | NOT NULL, UNIQUE | 番号 |
| director | TEXT | | 导演 |
| story | TEXT | | 自定义剧情 |
| release_date | TEXT | | 发布时间 |
| image_url | TEXT | | 封面图片路径 |
| video_url | TEXT | | 视频路径 |
| cn_title | TEXT | | 中文标题 |
| jp_title | TEXT | | 日文标题 |
| cn_story | TEXT | | 中文剧情 |
| jp_story | TEXT | | 日文剧情 |
| create_time | TEXT | DEFAULT now | 创建时间 |
| update_time | TEXT | DEFAULT now | 更新时间 |
| is_deleted | INTEGER | DEFAULT 0 | 软删除标记 |
| javtxt_id | INTEGER | | JavTxt 关联 ID |

**Trigger**: `update_work_timestamp` — 更新时自动刷新 `update_time`

---

### 2.2 actress — 女优表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| actress_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| birthday | TEXT | | 出生日期 |
| height | INTEGER | | 身高 (cm) |
| bust | INTEGER | | 胸围 |
| waist | INTEGER | | 腰围 |
| hip | INTEGER | | 臀围 |
| cup | TEXT | | 罩杯 |
| debut_date | TEXT | | 出道日期 |
| need_update | INTEGER | DEFAULT 1 | 1=需爬虫更新, 0=不需 |
| create_time | TEXT | DEFAULT now | 创建时间 |
| update_time | TEXT | DEFAULT now | 更新时间 |
| image_urlA | TEXT | | 头像 A |
| image_urlB | TEXT | | 头像 B |
| minnano_url | TEXT | | Minnano 链接 |

**Trigger**: `update_actress_timestamp` — 更新时自动刷新 `update_time`

---

### 2.3 actress_name — 女优姓名表

支持多艺名与名字变更链。

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| actress_name_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| actress_id | INTEGER | FK → actress | 女优 ID |
| name_type | INTEGER | | 1=主名, 0=非主名 |
| cn | TEXT | | 中文名 |
| jp | TEXT | | 日文名 |
| en | TEXT | | 英文名 |
| kana | TEXT | | 假名 |
| redirect_actress_name_id | INTEGER | FK → actress_name | 名字变更链，NULL 表示最新名 |

---

### 2.4 work_actress_relation — 作品-女优关系表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| work_actress_relation_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| work_id | INTEGER | FK → work, NOT NULL | 作品 ID |
| actress_id | INTEGER | FK → actress, NOT NULL | 女优 ID |
| job | TEXT | | 角色/职业（如职员、上司） |
| age | TEXT | | 年龄段（如年轻） |
| married | TEXT | | 人设（如人妻、女友） |
| state | TEXT | | 状态（如主动） |

---

### 2.5 manufacturer — 制作商表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| manufacturer_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| cn_name | TEXT | | 中文名 |
| jp_name | TEXT | | 日文名 |
| aliases | TEXT | | 别名 |
| detail | TEXT | | 其他信息 |
| logo_url | TEXT | | Logo 路径 |

> 注：迁移脚本中会迁移为 `maker` 表，`prefix_maker_relation` 引用 `maker`。

---

### 2.6 label — 厂牌表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| label_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| cn_name | TEXT | | 中文名 |
| jp_name | TEXT | | 日文名 |
| detail | TEXT | | 其他信息 |

---

### 2.7 tag — 标签表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| tag_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| tag_name | TEXT | NOT NULL, UNIQUE | 标签名 |
| tag_type_id | INTEGER | FK → tag_type | 标签类型 ID |
| color | TEXT | DEFAULT '#cccccc' | 颜色（hex） |
| redirect_tag_id | INTEGER | FK → tag | 重定向目标 tag_id |
| detail | TEXT | | 说明 |
| group_id | INTEGER | | 互斥组标记 |

---

### 2.8 work_tag_relation — 作品-标签关系表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| work_tag_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| work_id | INTEGER | FK → work | 作品 ID |
| tag_id | INTEGER | FK → tag | 标签 ID |

**UNIQUE**(work_id, tag_id) — 防止重复关联

---

### 2.9 actor — 男优表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| actor_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| birthday | TEXT | | 出生年份 |
| height | INTEGER | | 身高 |
| handsome | INTEGER | | 颜值 0/1/2 |
| fat | INTEGER | | 胖瘦 0/1/2 |
| need_update | INTEGER | DEFAULT 1 | 是否需更新 |
| create_time | TEXT | DEFAULT now | 创建时间 |

---

### 2.10 actor_name — 男优姓名表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| actor_name_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| actor_id | INTEGER | FK → actor | 男优 ID |
| name_type | INTEGER | | 1=主名, 0=非主名 |
| cn | TEXT | | 中文名 |
| jp | TEXT | | 日文名 |
| en | TEXT | | 英文名 |
| kana | TEXT | | 假名 |

---

### 2.11 work_actor_relation — 作品-男优关系表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| work_actor_relation_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| work_id | INTEGER | FK → work, NOT NULL | 作品 ID |
| actor_id | INTEGER | FK → actor, NOT NULL | 男优 ID |

---

### 2.12 prefix_maker_relation — 番号前缀-片商关系表

通过番号前缀映射到制作商。

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| prefix_maker_relation_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| prefix | TEXT | | 番号前缀（如 SSIS） |
| maker_id | INTEGER | FK → maker | 片商 ID |

---

## 三、私有库 (private.db)

### 3.1 db_version — 数据库版本表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| id | INTEGER | PK, AUTOINCREMENT | 主键 |
| version | TEXT | NOT NULL | 版本号 |
| applied_at | DATETIME | DEFAULT now | 应用时间 |
| description | TEXT | | 说明 |

---

### 3.2 favorite_actress — 收藏女优表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| favorite_actress_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| actress_id | INTEGER | UNIQUE, NOT NULL | 公共库 actress_id |
| jp_name | TEXT | NOT NULL | 日文名备份 |
| added_time | TEXT | NOT NULL, DEFAULT now | 收藏时间 |

---

### 3.3 favorite_work — 收藏作品表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| favorite_work_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| work_id | INTEGER | UNIQUE, NOT NULL | 公共库 work_id |
| serial_number | TEXT | UNIQUE, NOT NULL | 番号备份 |
| added_time | TEXT | NOT NULL, DEFAULT now | 收藏时间 |

---

### 3.4 love_making — 做爱记录表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| love_making_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| event_time | TEXT | | 时间（精确到小时） |
| rating | INTEGER | NOT NULL, CHECK(1-5) | 满意度 1–5 |
| comment | TEXT | | 评价 |
| create_time | TEXT | DEFAULT now | 创建时间 |
| update_time | TEXT | DEFAULT now | 更新时间 |

**Trigger**: `update_love_making_timestamp` — 更新时自动刷新 `update_time`

---

### 3.5 masturbation — 撸管记录表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| masturbation_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| work_id | INTEGER | | 关联作品 ID |
| serial_number | TEXT | | 番号冗余 |
| start_time | TEXT | | 起飞时间（精确到分钟） |
| tool_name | TEXT | | 工具（手/飞机杯等） |
| rating | INTEGER | NOT NULL, CHECK(1-5) | 满意度 1–5 |
| comment | TEXT | | 评论 |
| create_time | TEXT | DEFAULT now | 创建时间 |
| update_time | TEXT | DEFAULT now | 更新时间 |

---

### 3.6 sexual_arousal — 晨勃记录表

| 列名 | 类型 | 约束 | 说明 |
|------|------|------|------|
| sexual_arousal_id | INTEGER | PK, AUTOINCREMENT | 主键 |
| arousal_time | TEXT | | 晨勃时间（精确到小时） |
| comment | TEXT | | 评论（可含梦境） |
| create_time | TEXT | DEFAULT now | 创建时间 |
| update_time | TEXT | DEFAULT now | 更新时间 |

**Trigger**: `update_sexual_arousal_timestamp` — 更新时自动刷新 `update_time`

---

## 四、依赖表（未在上述 init 脚本中定义）

以下表被引用，但未在 `initTABLE.sql` / `initPrivateTable.sql` 中定义，可能来自种子数据或迁移脚本：

### tag_type — 标签类型表

| 列名 | 类型 | 说明 |
|------|------|------|
| tag_type_id | INTEGER | 主键 |
| tag_type_name | TEXT | 类型名称 |
| tag_order | INTEGER | 排序 |

### maker — 制作商表（由 manufacturer 迁移而来）

| 列名 | 类型 | 说明 |
|------|------|------|
| maker_id | INTEGER | 主键 |
| cn_name | TEXT | 中文名 |
| jp_name | TEXT | 日文名 |
| aliases | TEXT | 别名 |
| detail | TEXT | 说明 |
| logo_url | TEXT | Logo 路径 |

---

## 五、实体关系简图

```
┌─────────┐       ┌──────────────────────┐       ┌─────────┐
│  work   │───────│ work_actress_relation│───────│ actress │
└────┬────┘       └──────────────────────┘       └────┬────┘
     │                                                 │
     │            ┌──────────────────────┐            │
     └────────────│ work_tag_relation    │            │
     │            └──────────┬───────────┘            │
     │                       │                        │
     │            ┌──────────▼───────────┐            │
     │            │        tag           │            │
     │            └──────────┬───────────┘            │
     │                       │                        │
     │            ┌──────────▼───────────┐     ┌──────▼──────┐
     │            │     tag_type         │     │ actress_name│
     │            └──────────────────────┘     └─────────────┘
     │
     │            ┌──────────────────────┐       ┌─────────┐
     └────────────│ work_actor_relation  │───────│  actor  │
                  └──────────────────────┘       └────┬────┘
                                                      │
                                               ┌──────▼──────┐
     ┌──────────────────────┐                  │ actor_name │
     │ prefix_maker_relation│──────────────────└─────────────┘
     └──────────┬───────────┘
                │
     ┌──────────▼───────────┐
     │       maker          │
     └──────────────────────┘

【私有库 - 引用公共库 ID】
┌─────────────────┐     actress_id     ┌─────────┐
│ favorite_actress│───────────────────→│ actress │
└─────────────────┘                    └─────────┘

┌─────────────────┐     work_id        ┌─────┐
│ favorite_work   │───────────────────→│work │
└─────────────────┘                    └─────┘

┌──────────────┐    work_id (软引用)
│ masturbation │ ────────────────────→ work
└──────────────┘
```

---

## 六、跨库查询说明

私有库通过 `ATTACH DATABASE` 挂载为 `priv`，公共库为主库。查询示例：

```sql
-- 跨库：撸管记录关联作品
SELECT m.*, w.serial_number
FROM priv.masturbation m
LEFT JOIN work w ON m.work_id = w.work_id;
```
