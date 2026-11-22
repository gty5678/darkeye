
BEGIN TRANSACTION;

CREATE TABLE scene_actress_costume_item_relation(--场景演员穿的散件表
	scene_actress_costume_item_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	secene_actress_relation_id INTEGER,--演员场景关系表外键
	item_id INTEGER,--散件外键
	FOREIGN KEY(secene_actress_relation_id)REFERENCES scene_actress_relation(secene_actress_relation_id),
	FOREIGN KEY(item_id) REFERENCES costume_item(item_id)
);


INSERT INTO scene_actress_costume_item_relation (
    scene_actress_costume_item_relation_id, secene_actress_relation_id,item_id
)
SELECT 
    scene_actress_costum_set_item_relation_id, secene_actress_relation_id,item_id
FROM scene_actress_costum_set_item_relations;

--删除老表
DROP TABLE scene_actress_costum_set_item_relations;

--改名
--ALTER TABLE scene_actress_relations_new RENAME TO scene_actress_relation;

COMMIT;
