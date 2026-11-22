--修改tag表

COMMIT;
PRAGMA foreign_keys = OFF;

BEGIN TRANSACTION;

CREATE TABLE IF NOT EXISTS tag_new (--标签表
    tag_id INTEGER PRIMARY KEY AUTOINCREMENT, --主键
    tag_name TEXT UNIQUE NOT NULL,
    tag_type TEXT, -- 可选：如“主题”“体位”“制服”“剧情”分类，有的分类是互斥，有的是多选，比如体位是多选
	color TEXT DEFAULT '#cccccc',  -- 存 hex 颜色码（推荐）
	redirect_tag_id INTEGER, -- 自引用外键，解决tag的重定向问题
	detail TEXT,  --详细说明
	group_id INTEGER,   --互斥组标记
	FOREIGN KEY (redirect_tag_id) REFERENCES tag(tag_id)
);

INSERT INTO tag_new (
    tag_id,tag_name, tag_type, color, redirect_tag_id, detail,group_id
)
SELECT 
    tag_id,tag_name, tag_type, color, redirect_tag_id, detail,group_id
FROM tag;

DROP TABLE tag;

--改名
ALTER TABLE tag_new RENAME TO tag;

COMMIT;
PRAGMA foreign_keys = ON;