--修改tag表


PRAGMA foreign_keys = OFF;


CREATE TABLE IF NOT EXISTS work_tag_relation_new(--作品标签表 解决n对n的问题
	work_tag_id INTEGER PRIMARY KEY AUTOINCREMENT,--主键
	work_id INTEGER,--外键
	tag_id INTEGER,--外键
	FOREIGN KEY(work_id)REFERENCES work(work_id),
	FOREIGN KEY(tag_id)REFERENCES tag(tag_id),
	UNIQUE(work_id, tag_id) -- 防止插入重复对
);

INSERT INTO work_tag_relation_new (
    work_tag_id,work_id, tag_id
)
SELECT 
    work_tag_id,work_id, tag_id
FROM work_tag_relation;

DROP TABLE work_tag_relation;

--改名
ALTER TABLE work_tag_relation_new RENAME TO work_tag_relation;


PRAGMA foreign_keys = ON;