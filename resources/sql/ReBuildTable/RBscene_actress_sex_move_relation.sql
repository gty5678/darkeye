CREATE TABLE scene_actress_sex_move_relation(--场景演员做爱动作
	scene_actress_sex_move_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	secene_actress_relation_id INTEGER,--外键
	sex_move_id INTEGER, --外键
	number INTEGER,--顺序
	detail place TEXT,--细分地点
	undress TEXT, --脱衣程度
	FOREIGN KEY(secene_actress_relation_id)REFERENCES scene_actress_relation(secene_actress_relation_id),
	FOREIGN KEY(sex_move_id)REFERENCES sex_move(sex_move_id)
)