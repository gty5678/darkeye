

--这个是私有数据库从v1.0到v1.1的升级脚本
CREATE TABLE IF NOT EXISTS favorite_work_new(--收藏影片表
	favorite_work_id INTEGER PRIMARY KEY AUTOINCREMENT, --不重复主键
	work_id INTEGER NOT NULL,--外键，这个要与公共表中的work_id对应，但是要在软件层去解决数据一致性的问题，不需要唯一
	serial_number TEXT UNIQUE NOT NULL,--备份番号，避免公共表巨变时丢失work_id信息，这个作为最后的恢复手段
	added_time TEXT NOT NULL DEFAULT (DATETIME('now', 'localtime')) -- 收藏时间，这个写进去了就不更新了
);

INSERT INTO favorite_work_new (
    favorite_work_id, work_id, serial_number, added_time
)
SELECT 
	favorite_work_id, work_id, serial_number, added_time
FROM favorite_work;

--删除老表
DROP TABLE favorite_work;

--改名
ALTER TABLE favorite_work_new RENAME TO favorite_work;


