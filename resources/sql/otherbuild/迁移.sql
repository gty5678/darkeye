ATTACH DATABASE 'X:/AV_DB/resources/data/private.db' AS pdb;
ATTACH DATABASE 'X:/AV_DB/resources/data/av.db' AS src;
-- 复制表结构 + 数据
CREATE TABLE pdb.masturbate AS SELECT * FROM src.masturbate;
CREATE TABLE pdb.sexual_arousal AS SELECT * FROM src.sexual_arousal;
CREATE TABLE pdb.makelove AS SELECT * FROM src.makelove;

DETACH DATABASE pdb;