--修改masturbate名为masturbation

--创建临时的新的数据库
CREATE TABLE IF NOT EXISTS masturbation(
    masturbation_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    work_id INTEGER ,                           --作品id 一致性靠软件
    start_time TEXT,                                            --大致的起飞时间点，精确到分钟就行了，这个是外部输入的，因为一般都是事后记录
	tool_name TEXT,                                              --可以填手或者飞机杯的名字
	rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),  --满意度从1分到5分
	comment TEXT,                                           --评论对起飞的时间点进行评论
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);


INSERT INTO masturbation (
    masturbation_id,work_id, start_time, tool_name, rating, comment,create_time,update_time
)
SELECT 
    masturbate_id,work_id,"time", tool, rating, comment,create_time,update_time
FROM masturbate;

DROP TABLE masturbate;

--改名
--ALTER TABLE actress_name_new RENAME TO actress_name;
