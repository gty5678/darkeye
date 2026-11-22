BEGIN TRANSACTION;
INSERT INTO favorite_work(
    favorite_work_id, work_id, serial_number, added_time)
VALUES(1, 1, 'DEMO-001', '2026-01-02 10:00:00');
INSERT INTO favorite_actress(
    favorite_actress_id, actress_id, jp_name, added_time)
VALUES(1, 1, 'サンプル女優', '2026-01-02 10:05:00');
INSERT INTO masturbation(
    masturbation_id, work_id, serial_number, start_time, tool_name, rating,
    comment, create_time, update_time)
VALUES
    (1, 1, 'DEMO-001', '2026-01-02 21:30:00', 'fixture', 4, 'fixture',
     '2026-01-02 21:31:00', '2026-01-02 21:31:00'),
    (2, NULL, '', '2026-01-02 22:30:00', 'fixture', 3, '',
     '2026-01-02 22:31:00', '2026-01-02 22:31:00'),
    (3, 1, 'DEMO-001', '2026-01-03 20:00:00', 'fixture', 5, '',
     '2026-01-03 20:01:00', '2026-01-03 20:01:00');
INSERT INTO love_making(
    love_making_id, event_time, rating, comment, create_time, update_time)
VALUES(1, '2026-02-01 20:00:00', 4, 'fixture',
       '2026-02-01 20:01:00', '2026-02-01 20:01:00');
INSERT INTO sexual_arousal(
    sexual_arousal_id, arousal_time, comment, create_time, update_time)
VALUES(1, '2026-03-01 07:00:00', 'fixture',
       '2026-03-01 07:01:00', '2026-03-01 07:01:00');
COMMIT;
