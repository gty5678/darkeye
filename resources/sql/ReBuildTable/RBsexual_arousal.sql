--创建临时的新的数据库
CREATE TABLE IF NOT EXISTS sexual_arousal_new (                     --晨勃
    sexual_arousal_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    arousal_time TEXT,                                            --大致的晨勃时间，精准到小时就行了
	comment TEXT,                                           --评论对晨勃进行评论，可以把梦境写进去
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);


INSERT INTO sexual_arousal_new (
    sexual_arousal_id,arousal_time, comment, create_time,update_time
)
SELECT 
    sexual_arousal_id,"time", comment, create_time,update_time
FROM sexual_arousal;

DROP TABLE sexual_arousal;

--改名
ALTER TABLE sexual_arousal_new RENAME TO sexual_arousal;