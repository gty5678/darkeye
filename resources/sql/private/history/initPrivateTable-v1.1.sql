--这个是私有数据库的初始化脚本
--v1.1版本
BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS db_version (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL,
    applied_at DATETIME DEFAULT (datetime('now', 'localtime')),
    description TEXT
);
INSERT INTO db_version (version, applied_at, description) VALUES ('1.0', '2025-10-14 09:28:01', '初始版本');
INSERT INTO db_version (version, applied_at, description) VALUES ('1.1', '2026-03-12 09:28:01', '私有表结构更新');


CREATE TABLE IF NOT EXISTS favorite_actress(--收藏女优表
	favorite_actress_id INTEGER PRIMARY KEY AUTOINCREMENT, --不重复主键
	actress_id INTEGER NOT NULL,--外键，这个要与公共表中的actress_id对应，但是要在软件层去解决数据一致性的问题，不需要唯一
	jp_name TEXT NOT NULL,--备份日本姓名，避免公共表巨变时丢失actress_id信息，这个作为最后的恢复手段
	added_time TEXT NOT NULL DEFAULT (DATETIME('now', 'localtime')) -- 收藏时间，这个写进去了就不更新了
);

CREATE TABLE IF NOT EXISTS favorite_work(--收藏影片表
	favorite_work_id INTEGER PRIMARY KEY AUTOINCREMENT, --不重复主键
	work_id INTEGER NOT NULL,--外键，这个要与公共表中的work_id对应，但是要在软件层去解决数据一致性的问题，不需要唯一
	serial_number TEXT UNIQUE NOT NULL,--备份番号，避免公共表巨变时丢失work_id信息，这个作为最后的恢复手段
	added_time TEXT NOT NULL DEFAULT (DATETIME('now', 'localtime')) -- 收藏时间，这个写进去了就不更新了
);

CREATE TABLE IF NOT EXISTS love_making (--做爱表，现在似乎对我来说没有什么意义                     
    love_making_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    event_time TEXT,                                            --大致的做爱，精准到小时就行了
	rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),  --满意度从1分到5分
	comment TEXT,                                           --对做爱进行评价，这个是双应当是双方的满意度
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS "masturbation"(--撸管记录表
    masturbation_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    work_id INTEGER ,                           --作品id 一致性靠软件
	serial_number TEXT,   --冗余
    start_time TEXT,                                            --大致的起飞时间点，精确到分钟就行了，这个是外部输入的，因为一般都是事后记录
	tool_name TEXT,                                              --可以填手或者飞机杯的名字
	rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),  --满意度从1分到5分
	comment TEXT,                                           --评论对起飞的时间点进行评论
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS "sexual_arousal" (--晨勃记录表
    sexual_arousal_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    arousal_time TEXT,                                            --大致的晨勃时间，精准到小时就行了
	comment TEXT,                                           --评论对晨勃进行评论，可以把梦境写进去
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);



CREATE TRIGGER update_love_making_timestamp         --自动更新时间触发器
AFTER UPDATE ON love_making
FOR EACH ROW
BEGIN
    UPDATE love_making
    SET update_time = DATETIME('now', 'localtime')
    WHERE love_making_id = OLD.love_making_id;
END;

CREATE TRIGGER update_sexual_arousal_timestamp                 --自动更新时间触发器
AFTER UPDATE ON sexual_arousal
FOR EACH ROW
BEGIN
    UPDATE sexual_arousal
    SET update_time = DATETIME('now', 'localtime')
    WHERE sexual_arousal_id = OLD.sexual_arousal_id;
END;
COMMIT;
