
BEGIN TRANSACTION;
CREATE TABLE "scene_actress_relations_new"(--场景扮演角色关系表，附带发型，套装，情感，有无射
	secene_actress_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	scene_id INTEGER, --场景id外键
	actress_id INTEGER,--女优id外键
	hairstyle_id INTEGER,--发型id外键
	creampie TEXT,--内射？"内射/颜射/屁股/无/吞精"
	emotion TEXT, --情感的转变
	FOREIGN KEY(scene_id)REFERENCES scene(scene_id),
	FOREIGN KEY(actress_id)REFERENCES actress(actress_id),
	FOREIGN KEY(hairstyle_id)REFERENCES hairstyle(hairstyle_id)
);



INSERT INTO scene_actress_relations_new (
    secene_actress_relation_id, scene_id, actress_id, hairstyle_id, creampie, emotion
)
SELECT 
     secene_actress_relation_id, scene_id, actress_id, hairstyle_id, creampie, emotion
FROM scene_actress_relations;

--删除老表
DROP TABLE scene_actress_relations;

--改名
ALTER TABLE scene_actress_relations_new RENAME TO scene_actress_relation;

COMMIT;