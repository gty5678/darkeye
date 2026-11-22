

BEGIN TRANSACTION;
CREATE TABLE work_scene_relation_new (--作品场景关系表，一个作品包含多个场景
	"work_scene_relation_id" INTEGER PRIMARY KEY AUTOINCREMENT,
	"work_id"	INTEGER,
	"scene_id"	INTEGER,
	"number"	INTEGER,--第几个场景
	FOREIGN KEY("scene_id") REFERENCES scene("scene_id"),
	FOREIGN KEY("work_id") REFERENCES work("work_id")
);

INSERT INTO work_scene_relation_new (
    work_scene_relation_id, work_id, scene_id, number
)
SELECT 
     work_scene_relation_id, work_id, scene_id, number
FROM work_scene_relation;

--删除老表
DROP TABLE work_scene_relation;

--改名
ALTER TABLE work_scene_relation_new RENAME TO work_scene_relation;



COMMIT;