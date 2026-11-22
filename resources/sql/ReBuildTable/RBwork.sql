COMMIT;
--在运行命令前先保证已经写入更改了
PRAGMA foreign_keys = OFF;


BEGIN TRANSACTION;


-- 1.删除依赖的视图
DROP VIEW IF EXISTS v_work_all_info;
DROP VIEW IF EXISTS v_actress_all_info;
DROP VIEW IF EXISTS v_actress_movie_stats;
DROP VIEW IF EXISTS v_director;
DROP VIEW IF EXISTS v_scene_actress;
DROP VIEW IF EXISTS v_masturbate_byActress;

CREATE TABLE IF NOT EXISTS work_new (--公有作品表新的表
    work_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
    serial_number TEXT NOT NULL UNIQUE,--番号不能空不能重复
    director TEXT,                     --导演
    story TEXT,                        --这个主要是自己写的剧情
    release_date TEXT,                 --发布时间
    image_url TEXT,                    --图片地址
    video_url TEXT,                    --视频地址
	cn_title TEXT,                     --翻译标题
	jp_title TEXT,                     --官方标题
	cn_story TEXT,                     --翻译剧情
	jp_story TEXT,                     --官方剧情
	create_time TEXT DEFAULT (datetime('now', 'localtime')),
    update_time TEXT DEFAULT (datetime('now', 'localtime')),
	is_deleted INTEGER DEFAULT 0
);

INSERT INTO work_new (
    work_id, serial_number, director, story, release_date,
    image_url, video_url, cn_title,cn_story,jp_title,jp_story,create_time, update_time,is_deleted
)
SELECT 
    work_id, serial_number, director, story, release_date,
    image_url, video_url, cn_title,cn_story,jp_title,jp_story,create_time, update_time,is_deleted
FROM work;

DROP TABLE work;

--改名
ALTER TABLE work_new RENAME TO work;

--6.重建依赖的视图与触发器
--后面手动重建

COMMIT;

PRAGMA foreign_keys = ON;
