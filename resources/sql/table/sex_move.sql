BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS "sex_move"(--做爱动作表
	sex_move_id INTEGER PRIMARY KEY AUTOINCREMENT,
	sex_name TEXT, --各种姿势，包括亲吻，口，抚摸
	detail TEXT,--细节描述
	fVm TEXT --几女几男使用的
);
INSERT INTO "sex_move" VALUES (1,'拥抱',NULL,'1V1');
INSERT INTO "sex_move" VALUES (2,'亲吻',NULL,'1V1');
INSERT INTO "sex_move" VALUES (3,'摸胸',NULL,'1V1');
INSERT INTO "sex_move" VALUES (4,'摸下面',NULL,'1V1');
INSERT INTO "sex_move" VALUES (5,'亲耳朵',NULL,'1V1');
INSERT INTO "sex_move" VALUES (6,'亲屁股',NULL,'1V1');
INSERT INTO "sex_move" VALUES (7,'亲大腿',NULL,'1V1');
INSERT INTO "sex_move" VALUES (8,'亲胸',NULL,'1V1');
INSERT INTO "sex_move" VALUES (10,'传教式','男上女下','1V1');
INSERT INTO "sex_move" VALUES (11,'蜘蛛体位','男上女下，女生将双腿屈膝抬起，脚环抱男生的腰部；男生俯卧进入','1V1');
INSERT INTO "sex_move" VALUES (12,'V字体位','男上女下，女生将双腿抬高，呈V字形；男生抓住女生双腿。','1V1');
INSERT INTO "sex_move" VALUES (13,'I字体位','男上女下，女生将双腿抬高，呈I字形；男生抓住女生双腿。','1V1');
INSERT INTO "sex_move" VALUES (14,'抬腿体位','男上女下，女生将双腿抬高，跨到男生肩膀上；男生从中进入。','1V1');
INSERT INTO "sex_move" VALUES (15,'缩腿体位','男上女下，男生将女生双腿缩起挺入，女生臀部下方可垫一颗枕头，可增加进入深度。','1V1');
INSERT INTO "sex_move" VALUES (16,'女上体位',NULL,'1V1');
INSERT INTO "sex_move" VALUES (17,'反向女牛仔式',NULL,'1V1');
INSERT INTO "sex_move" VALUES (18,'深蹲女上体位',NULL,'1V1');
INSERT INTO "sex_move" VALUES (19,'侧坐女上',NULL,'1V1');
INSERT INTO "sex_move" VALUES (20,'后仰女上',NULL,'1V1');
INSERT INTO "sex_move" VALUES (21,'侧入',NULL,'1V1');
INSERT INTO "sex_move" VALUES (22,'汤勺','女生背对男生侧躺，双腿微弯，男生从身后进入，双手可环抱女生腰部。','1V1');
INSERT INTO "sex_move" VALUES (23,'反向汤勺','女生与男生头尾颠倒，并背对男生侧躺，双腿微弯；男生从身后进入，双手可环抱女生腿部。','1V1');
INSERT INTO "sex_move" VALUES (24,'剪刀脚','男女身体微曲，双腿打开交叉；除了男女之外，剪刀脚也是女女常用的体位，因为可直接摩擦阴部','1V1');
INSERT INTO "sex_move" VALUES (25,'抬腿侧入','女生侧躺，将上方的腿抬起，男生用手轻托女生腿部进入','1V1');
INSERT INTO "sex_move" VALUES (26,'后入',NULL,'1V1');
INSERT INTO "sex_move" VALUES (27,'熨斗式','女生面朝下躺在床上，双腿伸直，臀部稍微抬起；男生从后面进入。','1V1');
INSERT INTO "sex_move" VALUES (28,'双膝并拢后入','女生双膝并拢，趴伏在床上，臀部微抬；男生双脚打开从后方进入','1V1');
INSERT INTO "sex_move" VALUES (29,'跳蛙式','女生上半身伏趴在床上，臀部抬高，腿可跪着或蹲着；男生抱着女生臀部从后方进入','1V1');
INSERT INTO "sex_move" VALUES (30,'站着正入',NULL,'1V1');
INSERT INTO "sex_move" VALUES (31,'站着后入',NULL,'1V1');
INSERT INTO "sex_move" VALUES (32,'老汉推车','双方站着，女生弯腰前侵扶到地板，或扶着桌面或柜子；男生抓着女生脚踝，支撑女生腿部重量，从后方进入。','1V1');
INSERT INTO "sex_move" VALUES (33,'抬腿站立','女生单腿站立，另一条腿抬起搭在男生的臀部、腰部或肩膀上，男生用手托住抬起的腿以稳定姿势。','1V1');
INSERT INTO "sex_move" VALUES (34,'拉手推车','双方站着，女方弯腰双手后伸，男生抓住女生双手，协助分担重量；此姿势不建议做太久，避免女生手臂不适。','1V1');
INSERT INTO "sex_move" VALUES (35,'抱着爱爱','A片常见的体位，适合体力好、手臂力气大的男生；男生需稳稳抱着女生，避免失力让女生跌倒或折伤阴茎。','1V1');
INSERT INTO "sex_move" VALUES (36,'桌面站立','女生半坐在桌面、洗手台或其他支撑物上，双腿分开环抱男生腰部，男生站立进入。','1V1');
INSERT INTO "sex_move" VALUES (37,'坐跪式','男生坐在沙发上，女生抱着男生，腿张开M形做爱','1V1');
INSERT INTO "sex_move" VALUES (38,'反向坐跪',NULL,'1V1');
INSERT INTO "sex_move" VALUES (39,'坐跪式-抬腿','男生坐在沙发上，女生抱着男生，腿抬到肩上','1V1');
INSERT INTO "sex_move" VALUES (40,'69式','相互口','1V1');
INSERT INTO "sex_move" VALUES (41,'女口男','这个实际上有很多的形式，有的强迫','1V1');
INSERT INTO "sex_move" VALUES (42,'男舔女',NULL,'1V1');
INSERT INTO "sex_move" VALUES (43,'两面夹击','两男生站着，女生在中间，一面口，另一面被后入','1V2');

COMMIT;
