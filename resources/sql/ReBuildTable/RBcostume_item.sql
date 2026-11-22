--修改
COMMIT;
BEGIN TRANSACTION;
CREATE TABLE "costume_items_new" (--单体服饰表，单体的服装一定会非常的多，类别最好单独弄一个类
	item_id INTEGER PRIMARY KEY AUTOINCREMENT,
	item_name TEXT NOT NULL,            -- 单品名称(如"白色衬衫")
	color TEXT,
	item_category TEXT,					-- 类别，包括"上衣""下装""连体外装""袜类""鞋子""首饰""文胸""内裤""头部配饰""连体内衣""手部配饰""颈部配饰" "腿部配饰" "腰部配饰"
	position_order INTEGER,             -- 穿着层次顺序(0-最内层)
	notes TEXT                         -- 全名称
);


INSERT INTO costume_items_new (
    item_id, item_name, color, item_category, position_order, notes
)
SELECT 
	item_id, item_name, color, item_category, position_order, notes
FROM costume_items;

--删除老表
DROP TABLE costume_items;

--改名
ALTER TABLE costume_items_new RENAME TO costume_item;


COMMIT;
