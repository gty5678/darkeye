BEGIN TRANSACTION;
INSERT INTO tag_type(tag_type_id, tag_type_name, tag_order)
VALUES(1, '示例类型', 10);
INSERT INTO tag(tag_id, tag_name, tag_type_id, color, detail)
VALUES(1, '标签甲', 1, '#336699', '脱敏测试标签'),
      (2, '标签乙', 1, '#663399', 'Unicode ✓');
INSERT INTO maker(maker_id, cn_name, jp_name, aliases, detail)
VALUES(1, '示例制作商', 'サンプルメーカー', 'DEMO', 'fixture');
INSERT INTO prefix_maker_relation(prefix_maker_relation_id, prefix, maker_id)
VALUES(1, 'DEMO', 1);
INSERT INTO label(label_id, cn_name, jp_name, aliases, detail)
VALUES(1, '示例厂牌', 'サンプルレーベル', '标签别名', 'fixture');
INSERT INTO series(series_id, cn_name, jp_name, aliases, detail, related_series)
VALUES(1, '示例系列', 'サンプルシリーズ', '系列别名', 'fixture', NULL);
INSERT INTO actress(
    actress_id, birthday, height, bust, waist, hip, cup, debut_date, notes,
    need_update, create_time, update_time, image_urlA, image_urlB, minnano_url)
VALUES(
    1, '2000-01-02', 165, 88, 58, 87, 'D', '2020-01-01', '脱敏人物',
    0, '2026-01-01 00:00:00', '2026-01-01 00:00:00',
    'https://invalid.example/actress-a.jpg', NULL, NULL);
INSERT INTO actress_name(
    actress_name_id, actress_id, name_type, cn, jp, en, kana,
    redirect_actress_name_id)
VALUES(1, 1, 1, '示例女演员', 'サンプル女優', 'Example Actress', 'さんぷる', NULL);
INSERT INTO actor(
    actor_id, birthday, height, handsome, fat, notes, need_update, create_time,
    image_url)
VALUES(
    1, '1980-03-04', 180, 1, 1, '脱敏人物', 0, '2026-01-01 00:00:00',
    'https://invalid.example/actor-a.jpg');
INSERT INTO actor_name(actor_name_id, actor_id, name_type, cn, jp, en, kana)
VALUES(1, 1, 1, '示例男演员', 'サンプル男優', 'Example Actor', 'さんぷる');
INSERT INTO work(
    work_id, serial_number, director, runtime, notes, release_date, image_url,
    video_url, cn_title, jp_title, cn_story, jp_story, maker_id, label_id,
    series_id, fanart, create_time, update_time, is_deleted, javtxt_id,
    fcover_url, on_dan)
VALUES(
    1, 'DEMO-001', '示例导演', 120, '第一条脱敏记录', '2025-01-02',
    'https://invalid.example/cover-1.jpg', 'D:/fixture/DEMO-001.mp4',
    '示例作品一', 'サンプル作品一', '中文简介', '日本語の概要',
    1, 1, 1,
    '[{"url":"https://invalid.example/fanart-1.jpg","file":"DEMO-001-1.jpg"}]',
    '2026-01-01 00:00:00', '2026-01-01 00:00:00', 0, NULL, NULL, 0),
    (
    2, 'DEMO-002', NULL, NULL, '', '2025-02-03', NULL, NULL,
    '已删除示例', '', '', '', NULL, NULL, NULL, '[]',
    '2026-01-02 00:00:00', '2026-01-02 00:00:00', 1, NULL, NULL, NULL);
INSERT INTO work_actress_relation(
    work_actress_relation_id, work_id, actress_id, job, age, married, state)
VALUES(1, 1, 1, '职员', '年轻', NULL, '主动');
INSERT INTO work_actor_relation(work_actor_relation_id, work_id, actor_id)
VALUES(1, 1, 1);
INSERT INTO work_tag_relation(work_tag_id, work_id, tag_id)
VALUES(1, 1, 1), (2, 1, 2);
COMMIT;
