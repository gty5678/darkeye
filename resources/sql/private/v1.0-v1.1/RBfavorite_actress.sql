


--这个是私有数据库从v1.0到v1.1的升级脚本
CREATE TABLE IF NOT EXISTS favorite_actress_new(--收藏女优表
	favorite_actress_id INTEGER PRIMARY KEY AUTOINCREMENT, --不重复主键
	actress_id INTEGER NOT NULL,--外键，这个要与公共表中的actress_id对应，但是要在软件层去解决数据一致性的问题，不需要唯一
	jp_name TEXT NOT NULL,--备份日本姓名，避免公共表巨变时丢失actress_id信息，这个作为最后的恢复手段
	added_time TEXT NOT NULL DEFAULT (DATETIME('now', 'localtime')) -- 收藏时间，这个写进去了就不更新了
);


INSERT INTO favorite_actress_new (
    favorite_actress_id, actress_id, jp_name, added_time
)
SELECT 
	favorite_actress_id, actress_id, jp_name, added_time
FROM favorite_actress;

--删除老表
DROP TABLE favorite_actress;

--改名
ALTER TABLE favorite_actress_new RENAME TO favorite_actress;



