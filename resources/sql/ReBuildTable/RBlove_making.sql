

CREATE TABLE IF NOT EXISTS love_making (--做爱表，现在似乎对我来说没有什么意义                     
    love_making_id INTEGER PRIMARY KEY AUTOINCREMENT,        --不重复主键
    event_time TEXT,                                            --大致的做爱，精准到小时就行了
	rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),  --满意度从1分到5分
	comment TEXT,                                           --对做爱进行评价，这个是双应当是双方的满意度
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime'))
);


INSERT INTO love_making (
    love_making_id,event_time, rating, comment,create_time,update_time
)
SELECT 
    makelove_id,"time", rating, comment,create_time,update_time
FROM makelove;

DROP TABLE makelove;