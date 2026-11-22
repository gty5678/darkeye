--修改actress
--女演员


BEGIN TRANSACTION;

CREATE TABLE "actress_new"(
	actress_id INTEGER PRIMARY KEY AUTOINCREMENT,
	birthday TEXT,                                    --出生日期
	height INTEGER,                                   --身高
	bust INTEGER,                                     --胸围
	waist INTEGER,                                    --腰围
	hip INTEGER,                                      --臀围
	cup TEXT,                                         --罩杯
	debut_date TEXT,                                  --出道日期
	need_update INTEGER DEFAULT 1,                    --是否需要爬虫更新数据,创建后默认需要更新 1代表需要更新 0代表不需要
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);


INSERT INTO actress_new (
    actress_id,birthday, height, bust, waist, hip,cup
)
SELECT 
    actress_id,birthday, height, bust, waist, hip,cup
FROM actress;

DROP TABLE actress;

--改名
ALTER TABLE actress_new RENAME TO actress;

COMMIT;
