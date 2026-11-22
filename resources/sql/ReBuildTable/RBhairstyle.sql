--修改hairstyle
COMMIT;
BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS hairstyles_new( --发型表
	hairstyle_id INTEGER PRIMARY KEY AUTOINCREMENT,
	color TEXT,--颜色
	"length" TEXT,--"长中短"
	curl TEXT, --"卷/直/直末端卷/大卷"
	bundle TEXT,--束发的方式"单马尾/双马尾/丸子头"
	bangs TEXT--刘海
);


INSERT INTO hairstyles_new (
    hairstyle_id, color, "length", curl, bundle, bangs
)
SELECT 
    hairstyle_id, color, "length", curl, bundle, bangs
FROM hairstyles;

--删除老表
DROP TABLE hairstyles;

--改名
ALTER TABLE hairstyles_new RENAME TO hairstyle;

COMMIT;