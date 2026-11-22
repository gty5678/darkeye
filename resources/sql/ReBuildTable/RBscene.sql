PRAGMA foreign_keys = OFF;

BEGIN TRANSACTION;


CREATE TABLE scenes_new( --场景表
	scene_id INTEGER PRIMARY KEY AUTOINCREMENT,
	place TEXT, --地点"酒店房间/酒店浴室/私人办公室"
	scene_time TEXT,--表演时间"晚上/第二天早上"
	plot TEXT,--剧情
	light TEXT, --灯光 "冷/暖"
	category TEXT   --"短做爱/长做爱/剧情"
);

INSERT INTO scenes_new (
    scene_id, place, scene_time, plot, light, category
)
SELECT 
    scene_id, place, scene_time, plot, light, category
FROM scene;

--删除老表
DROP TABLE scene;

--改名
ALTER TABLE scenes_new RENAME TO scene;



COMMIT;