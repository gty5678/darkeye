
--rubbish
---被抛弃的表

CREATE TABLE IF NOT EXISTS scene( --场景表
	scene_id INTEGER PRIMARY KEY AUTOINCREMENT,
	place TEXT, --地点"酒店房间/酒店浴室/私人办公室"
	scene_time TEXT,--表演时间"晚上/第二天早上"
	plot TEXT,--剧情
	light TEXT, --灯光 "冷/暖"
	category TEXT   --"短做爱/长做爱/剧情"
);

CREATE TABLE IF NOT EXISTS work_scene_relation(--作品场景关系表
	work_scene_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	work_id INTEGER,--外键
	scene_id INTEGER,--外键
	number INTEGER,--单部作品中的序号
	FOREIGN KEY(work_id) REFERENCES work(work_id),
	FOREIGN KEY(scene_id) REFERENCES scene(scene_id)
);

CREATE TABLE IF NOT EXISTS scene_actress_relation(--场景扮演角色关系表，附带发型，套装，情感，有无射
	secene_actress_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	scene_id INTEGER, --场景id外键
	actress_id INTEGER,--女优id外键
	hairstyle_id INTEGER,--发型id外键
	creampie TEXT,--内射？"内射/颜射/屁股/无/吞精"
	emotion TEXT, --情感的转变
	FOREIGN KEY(scene_id)REFERENCES scene(scene_id),
	FOREIGN KEY(actress_id)REFERENCES actress(actress_id),
	FOREIGN KEY(hairstyle_id)REFERENCES hairstyle(hairstyle_id)
);
CREATE TABLE IF NOT EXISTS scene_actress_costume_item_relation(--场景演员穿的散件表
	scene_actress_costume_item_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	secene_actress_relation_id INTEGER,--演员场景关系表外键
	item_id INTEGER,--散件外键
	FOREIGN KEY(secene_actress_relation_id)REFERENCES scene_actress_relation(secene_actress_relation_id),
	FOREIGN KEY(item_id) REFERENCES costume_item(item_id)
);

CREATE TABLE IF NOT EXISTS scene_actress_sex_move_relation(--场景演员做爱动作
	scene_actress_sex_move_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	secene_actress_relation_id INTEGER,--外键
	sex_move_id INTEGER, --外键
	number INTEGER,--顺序
	detail place TEXT,--细分地点
	undress TEXT, --脱衣程度
	FOREIGN KEY(secene_actress_relation_id)REFERENCES scene_actress_relation(secene_actress_relation_id),
	FOREIGN KEY(sex_move_id)REFERENCES sex_move(sex_move_id)
);

--统计场景演员
--DROP VIEW IF EXISTS v_scene_actress;
CREATE VIEW IF NOT EXISTS v_scene_actress AS
SELECT 
	w.work_id,
	w.serial_number AS 番号,
	--wsr.scene_id,
	wsr.number AS 场景号,
	sce.scene_time AS 发生时间,
	sar.emotion AS 情绪,
	sar.secene_actress_relation_id,
	sce.place AS 发生地点,
	sce.plot AS 剧情,
	sar.creampie AS 射精情况,
	sce.category,
	(SELECT cn FROM actress_name WHERE actress_id = sar.actress_id AND(name_type=1)) AS 演员,
	--group_concat(c.item_name,',') AS 衣着,--普通版本
	COALESCE(NULLIF(group_concat(c.item_name,','), ''), '裸体') AS 衣着,
	h.color || h."length"||h.curl AS 发基,
	h.bundle,
	h.bangs
FROM
	work w
JOIN
	work_scene_relation wsr ON wsr.work_id=w.work_id
LEFT JOIN
	scene sce ON sce.scene_id=wsr.scene_id
LEFT JOIN
	scene_actress_relation sar ON sar.scene_id=wsr.scene_id
LEFT JOIN
	scene_actress_costume_item_relation sacir ON sacir.secene_actress_relation_id=sar.secene_actress_relation_id
LEFT JOIN
	hairstyle h ON h.hairstyle_id=sar.hairstyle_id
LEFT JOIN
	costume_item c ON c.item_id=sacir.item_id
--WHERE sce.category = '长做爱'  -- 添加筛选条件
GROUP BY
	sar.secene_actress_relation_id
ORDER BY
	w.work_id ASC,wsr.number ASC;
	
--统计导演在我的数据库里的拍片的数量
--DROP VIEW IF EXISTS v_director;
CREATE VIEW IF NOT EXISTS v_director AS
SELECT 
    director AS 导演,
    COUNT(*) AS 影片数量,
	MIN(release_date) AS first_movie_date,
    MAX(release_date) AS latest_movie_date
FROM 
    work
WHERE 
    director IS NOT NULL  -- 排除导演为NULL的记录
	AND director != '----' --排除找不到数据的导演
GROUP BY 
    director
ORDER BY 
    影片数量 DESC;

--统计每个女优的撸管次数
--由于个人数据不是很丰富，所以最后这一列的意义不是很大
--DROP VIEW IF EXISTS v_masturbate_byActress;
CREATE VIEW IF NOT EXISTS v_masturbate_byActress AS
SELECT 
    a.actress_id,
    -- 当前使用现用名
    (SELECT cn FROM actress_name WHERE actress_id = a.actress_id AND name_type = 1) AS actress_name,
    COUNT(m.work_id) AS masturbate,
	MAX(m.time) AS latest_masturbate_time,
    -- 找出当前女优被撸次数最多的作品的serial_number（用 LIMIT 1 只取一个）
    (
        SELECT w.serial_number
        FROM masturbate m1
        JOIN work_actress_relation war1 ON m1.work_id = war1.work_id
		JOIN work w ON w.work_id=m1.work_id
        WHERE war1.actress_id = a.actress_id
        GROUP BY m1.work_id
        ORDER BY COUNT(*) DESC
        LIMIT 1
    ) AS top_work_serial_number

FROM 
    actress a
LEFT JOIN work_actress_relation war ON a.actress_id = war.actress_id
LEFT JOIN work w ON war.work_id = w.work_id
LEFT JOIN masturbate m ON m.work_id = w.work_id
GROUP BY a.actress_id
ORDER BY 
masturbate DESC,
latest_masturbate_time DESC;  -- 次要排序：时间降序（最近的在前）