BEGIN TRANSACTION;

CREATE TABLE IF NOT EXISTS "work_actress_relation_new"(  --作品女演员表，主要解决一个作品有多个女演员的问题，多对多
	work_actress_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	work_id INTEGER NOT NULL,
	actress_id INTEGER NOT NULL,
	job TEXT, --"职员""上司""老板"
	age TEXT, --"年轻"
	married TEXT, --"人妻" "女友"
	state TEXT,  --"主动"
	FOREIGN KEY(work_id) REFERENCES work(work_id),
	FOREIGN KEY(actress_id) REFERENCES actress(actress_id)
);

INSERT INTO work_actress_relation_new (
    work_actress_relation_id, work_id,  actress_id, job, age, married,state
)
SELECT 
    work_actress_relation_id, work_id,  actress_id, job, age, married,state
FROM work_actress_relation;

DROP TABLE work_actress_relation;

--改名
ALTER TABLE work_actress_relation_new RENAME TO work_actress_relation;
COMMIT;