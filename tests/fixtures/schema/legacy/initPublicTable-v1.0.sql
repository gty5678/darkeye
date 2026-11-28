BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS actor(    				--男优表，主要是筛选男的帅不帅，有的实在是太丑了，又丑又胖,
	actor_id INTEGER PRIMARY KEY AUTOINCREMENT,			--自增id
	birthday   TEXT,                                    --出生年份-大致的
	height   INTEGER,                                   --身高
	handsome  INTEGER,		--0分丑，1分正常，2分帅
	fat       INTEGER,       --0分胖，1分正常，2分瘦
	need_update INTEGER DEFAULT 1,                    --男优数据没有地方去查，默认都没有，但是留着吧
	create_time TEXT DEFAULT (datetime('now', 'localtime'))
, image_url TEXT);
CREATE TABLE IF NOT EXISTS actor_name(   --男优姓名表,解决男优多艺名问题
	actor_name_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
	actor_id INTEGER,--男id-这个是外键
	name_type INTEGER,--1主名，0代表非主名
	cn TEXT,--中文名
	jp TEXT,--日文名
	en TEXT,--英文名
	kana TEXT,--假名
	FOREIGN KEY(actor_id) REFERENCES actor(actor_id)
);
CREATE TABLE IF NOT EXISTS "actress"(
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
, image_urlA TEXT, image_urlB TEXT, minnano_url TEXT);
CREATE TABLE IF NOT EXISTS "actress_name"(   --女优姓名表,解决女优多艺名问题
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
CREATE TABLE IF NOT EXISTS db_version (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL,
    applied_at DATETIME DEFAULT (datetime('now', 'localtime')),
    description TEXT
);
INSERT INTO "db_version" VALUES (1,'1.0','2025-10-14 09:28:01','初始版本数据库');

CREATE TABLE IF NOT EXISTS label(--厂牌，レーベル
	label_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
	cn_name TEXT,										--中文名
	jp_name TEXT,										--日文名
	detail TEXT                                       --其他信息
);
CREATE TABLE IF NOT EXISTS maker(--制作商，一部片子只有一个制作商
	maker_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
	cn_name TEXT,										--中文名
	jp_name TEXT,										--日文名
	aliases TEXT,										--别名
	detail TEXT,                                       --其他信息
	logo_url TEXT										--logo地址
);
CREATE TABLE IF NOT EXISTS prefix_maker_relation(--番号前缀与片商的对应关系
	prefix_maker_relation_id INTEGER PRIMARY KEY AUTOINCREMENT,
	prefix TEXT,--番号前缀
	maker_id INTEGER,--外键，这个要与maker表中的maker_id对应
	FOREIGN KEY(maker_id)REFERENCES maker(maker_id)
);
CREATE TABLE IF NOT EXISTS "tag" (--标签表
    tag_id INTEGER PRIMARY KEY AUTOINCREMENT, --主键
    tag_name TEXT UNIQUE NOT NULL,
    tag_type_id INTEGER, -- 可选：如“主题”“体位”“制服”“剧情”分类，有的分类是互斥，有的是多选，比如体位是多选
	color TEXT DEFAULT '#cccccc',  -- 存 hex 颜色码（推荐）
	redirect_tag_id INTEGER, -- 自引用外键，解决tag的重定向问题
	detail TEXT,  --详细说明
	group_id INTEGER,   --互斥组标记
	FOREIGN KEY (redirect_tag_id) REFERENCES tag(tag_id),
	FOREIGN KEY (tag_type_id) REFERENCES tag_type(tag_type_id)
);
CREATE TABLE IF NOT EXISTS tag_type (--标签表
    tag_type_id INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE, --主键
	tag_type_name TEXT UNIQUE,--标签类型名不能重复
	tag_order INTEGER--标签类型编号
);
CREATE TABLE IF NOT EXISTS "work" (--公有作品表新的表
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
	create_time TEXT DEFAULT (datetime('now', 'localtime')), --创建时间
    update_time TEXT DEFAULT (datetime('now', 'localtime')), --更新时间
	is_deleted INTEGER DEFAULT 0,       --是否删除，0代表未删除，1代表删除
	javtxt_id INT, --javtxt_id ，缓存的
	fcover_url TEXT, --封面图片地址，缓存的
	on_dan INTEGER  --是否在avdanyuwiki上找到，缓存的
);

CREATE TABLE IF NOT EXISTS work_actor_relation(  --作品男演员表，多对多，不要有其他数据，没有意义
	work_actor_relation_id INTEGER PRIMARY KEY AUTOINCREMENT, --自增主键
	work_id INTEGER NOT NULL, --外键，这个要与work表中的work_id对应
	actor_id INTEGER NOT NULL, --外键，这个要与actor表中的actor_id对应
	FOREIGN KEY(work_id) REFERENCES work(work_id),
	FOREIGN KEY(actor_id) REFERENCES actor(actor_id)
);
CREATE TABLE IF NOT EXISTS "work_actress_relation"(  --作品女演员表，主要解决一个作品有多个女演员的问题，多对多
	work_actress_relation_id INTEGER PRIMARY KEY AUTOINCREMENT, --自增主键
	work_id INTEGER NOT NULL, --外键，这个要与work表中的work_id对应
	actress_id INTEGER NOT NULL, --外键，这个要与actress表中的actress_id对应
	job TEXT,                    --"职员""上司""老板"
	age TEXT,                    --"年轻"
	married TEXT,                --"人妻" "女友"
	state TEXT,                  --"主动"
	FOREIGN KEY(work_id) REFERENCES work(work_id),
	FOREIGN KEY(actress_id) REFERENCES actress(actress_id)
);
CREATE TABLE IF NOT EXISTS "work_tag_relation"(--作品标签表 解决n对n的问题
	work_tag_id INTEGER PRIMARY KEY AUTOINCREMENT,--主键
	work_id INTEGER,                         --外键
	tag_id INTEGER,                          --外键
	FOREIGN KEY(work_id)REFERENCES work(work_id),
	FOREIGN KEY(tag_id)REFERENCES tag(tag_id),
	UNIQUE(work_id, tag_id) -- 防止插入重复对
);

CREATE VIEW v_actor_all_info AS--查询男优的基本数据的视图
SELECT 
    actor_id AS "男优ID",
    (SELECT cn FROM actor_name WHERE actor_id = a.actor_id AND(name_type=1)) AS "中文名",
	(SELECT jp FROM actor_name WHERE actor_id = a.actor_id AND(name_type=1)) AS "日文名",

    birthday AS "出生日期",
	handsome AS "帅气程度",
	fat AS "胖瘦"
FROM actor a;
CREATE VIEW v_actress_all_info AS--查询女优的基本数据的视图
SELECT 
    actress_id AS "女优ID",
    (SELECT cn FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)) AS "中文名",
	(SELECT jp FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)) AS "日文名",
	(SELECT en FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)) AS "英文名",
	(SELECT kana FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)) AS "假名",
    (
        SELECT GROUP_CONCAT(cn, ',') 
        FROM actress_name 
        WHERE actress_id = a.actress_id AND (name_type = 3 OR name_type=4)
		) AS "别名",
    birthday AS "出生日期",
    height AS "身高(cm)",
    bust AS "胸围(cm)",
    waist AS "腰围(cm)",
    hip AS "臀围(cm)",
    cup AS "罩杯",
	debut_date AS "出道日期",
	round((julianday(debut_date)-julianday(birthday))/365.25,1)-0.25 AS "出道年龄",
	need_update
FROM actress a;

CREATE VIEW v_actress_movie_stats AS--查询女优的作品统计数据的视图
SELECT 
    a.actress_id,
    (SELECT cn FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)) AS actress_name,
    COUNT(war.work_id) AS total_movies,
    MIN(w.release_date) AS first_movie_date,
    MAX(w.release_date) AS latest_movie_date
FROM 
    actress a
LEFT JOIN work_actress_relation war ON a.actress_id = war.actress_id
LEFT JOIN work w ON war.work_id = w.work_id
GROUP BY a.actress_id
ORDER BY total_movies DESC;

CREATE VIEW v_work_all_info AS--查询作品的基本数据的视图，这个非常的长
WITH actress_age_at_release AS (--计算每个女优发布作品的年龄
  SELECT
    w.work_id,
    a.actress_id,
    w.serial_number,
    w.release_date,
    a.birthday,
    -- 使用 julianday 计算日期差（以天为单位），然后除以 365.25 得到年龄
    (julianday(w.release_date) - julianday(a.birthday)) / 365.25 AS age_at_release
  FROM work w
  JOIN work_actress_relation war ON w.work_id = war.work_id
  JOIN actress a ON war.actress_id = a.actress_id
  WHERE w.release_date IS NOT NULL AND a.birthday IS NOT NULL
),
average_age_per_work AS (--辅助计算年龄的表
  SELECT
    work_id,
    serial_number,
    ROUND(AVG(age_at_release), 1)-0.45 AS avg_age_at_release--假设拍摄后5个多月发布
  FROM actress_age_at_release
  GROUP BY work_id
),
actress_list AS(--计算女优出演的名单
SELECT
	w.work_id,
    GROUP_CONCAT(
        (SELECT cn FROM actress_name WHERE actress_id = a.actress_id AND(name_type=1)),
        ','
    ) AS actress_list,
	GROUP_CONCAT(war.job,',') AS job,
	GROUP_CONCAT(war.state,',') AS state
FROM
    work w
LEFT JOIN 
    work_actress_relation war ON w.work_id = war.work_id
LEFT JOIN 
    actress a ON war.actress_id = a.actress_id
GROUP BY w.work_id
),
actor_list AS(--男优名单
SELECT
	w.work_id,
    GROUP_CONCAT(
        (SELECT cn FROM actor_name WHERE actor_id=war1.actor_id),
        ','
    ) AS actor_list
FROM
    work w
LEFT JOIN 
    work_actor_relation war1 ON w.work_id = war1.work_id
LEFT JOIN 
    actor a ON war1.actor_id = a.actor_id
GROUP BY w.work_id
),
studio_list AS(--片商表
SELECT 
	w.work_id,
	(SELECT cn_name FROM maker WHERE maker_id =p.maker_id) AS studio_name
FROM 
    work w
INNER JOIN 
    prefix_maker_relation p ON p.prefix = SUBSTR(w.serial_number, 1, INSTR(w.serial_number, '-') - 1)
WHERE 
    w.serial_number LIKE '%-%'
)
SELECT --水平计算表，然后统一合并
	w.work_id,
    w.serial_number AS serial_number,
    w.director AS director,
	w.release_date AS release_date,
	(SELECT actress_list FROM actress_list WHERE work_id=w.work_id)AS actress,
	(SELECT avg_age_at_release FROM average_age_per_work WHERE work_id=w.work_id)AS avg_age,
	(SELECT state FROM actress_list WHERE work_id=w.work_id)AS state,
	(SELECT actor_list FROM actor_list WHERE work_id=w.work_id)AS actor,
	w.story AS story,
	w.cn_title,
	w.cn_story,
	w.jp_title,
	w.jp_story,
	(SELECT studio_name FROM studio_list WHERE work_id=w.work_id)AS studio
FROM 
    work w;
CREATE TRIGGER update_actress_timestamp                                  --自动更新时间触发器
AFTER UPDATE ON actress
FOR EACH ROW
BEGIN
    UPDATE actress 
    SET update_time = DATETIME('now', 'localtime')
    WHERE actress_id = OLD.actress_id;
END;
CREATE TRIGGER update_work_timestamp                                  --自动更新时间触发器
AFTER UPDATE ON work
FOR EACH ROW
BEGIN
    UPDATE work 
    SET update_time = DATETIME('now', 'localtime')
    WHERE work_id = OLD.work_id;
END;
COMMIT;
