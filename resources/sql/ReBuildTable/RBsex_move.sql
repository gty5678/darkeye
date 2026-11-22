

BEGIN TRANSACTION;

CREATE TABLE sex_moves_new(--做爱动作表
	sex_move_id INTEGER PRIMARY KEY AUTOINCREMENT,
	sex_name TEXT, --各种姿势，包括亲吻，口，抚摸
	detail TEXT,--细节描述
	fVm TEXT --几女几男使用的
);

INSERT INTO sex_moves_new (
    sex_move_id, sex_name, detail, fVm
)
SELECT 
     sex_move_id, sex_name, detail, fVm
FROM sex_moves;

--删除老表
DROP TABLE sex_moves;

--改名
ALTER TABLE sex_moves_new RENAME TO sex_move;

COMMIT;