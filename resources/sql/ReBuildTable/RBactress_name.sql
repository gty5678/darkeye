--修改actress
--女演员
COMMIT;

BEGIN TRANSACTION;

--创建临时的新的数据库
CREATE TABLE IF NOT EXISTS actress_name_new(   --女优姓名表,解决女优多艺名问题
	actress_name_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
	actress_id INTEGER,--女优id-这个是外键
	name_type INTEGER,--1主名，0代表非主名，最新的名字根据下面的链条算出来
	cn TEXT,
	jp TEXT,
	en TEXT,
	kana TEXT,
	redirect_actress_name_id INTEGER,--自引用外键，解决名字的链条问题，这个是NULL说明是最新的名字
	FOREIGN KEY(actress_id) REFERENCES actress(actress_id)
	FOREIGN KEY(redirect_actress_name_id) REFERENCES actress_name(actress_name_id)
);


INSERT INTO actress_name_new (
    actress_name_id,actress_id, name_type, cn, jp, en,kana
)
SELECT 
    actress_name_id,actress_id, name_type, cn, jp, en,kana
FROM actress_name;

DROP TABLE actress_name;

--改名
ALTER TABLE actress_name_new RENAME TO actress_name;

COMMIT;
