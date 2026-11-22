

--创建临时的新的数据表
CREATE TABLE IF NOT EXISTS maker(--制作商，一部片子只有一个制作商
	maker_id INTEGER PRIMARY KEY AUTOINCREMENT,--不重复主键
	cn_name TEXT,										--中文名
	jp_name TEXT,										--日文名
	aliases TEXT,										--别名
	detail TEXT,                                       --其他信息
	logo_url TEXT										--logo地址
);


INSERT INTO maker (
    maker_id,cn_name,jp_name,aliases,detail,logo_url
)
SELECT 
    manufacturer_id,cn_name,jp_name,aliases,detail,logo_url
FROM manufacturer;

DROP TABLE manufacturer;

--改名
--ALTER TABLE manufacturer RENAME TO actress_name;