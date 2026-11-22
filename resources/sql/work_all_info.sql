-- 作品汇总查询。
-- 注意：库内完整度（15 位 bit 串 + 分值）顺序需与
-- core/database/query/work_completeness.py::WORK_COMPLETENESS_KEYS 保持一致。
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
tag_list AS(--标签列表
SELECT
    w.work_id,
    GROUP_CONCAT(t.tag_name, '|') AS tag_list
FROM
    work w
LEFT JOIN
    work_tag_relation wtr ON w.work_id = wtr.work_id
LEFT JOIN
    tag t ON wtr.tag_id = t.tag_id
GROUP BY w.work_id
),
maker_label_series_list AS(--片商/发行商/系列表
SELECT
    w.work_id,
    (SELECT cn_name FROM maker WHERE maker_id = w.maker_id) AS maker_name,
    (SELECT cn_name FROM label WHERE label_id = w.label_id) AS label_name,
    (SELECT cn_name FROM series WHERE series_id = w.series_id) AS series_name
FROM 
    work w
),
work_completeness_flags AS (
SELECT
    w.work_id,
    CASE WHEN TRIM(COALESCE(w.image_url, '')) <> '' THEN 1 ELSE 0 END AS f_cover,
    CASE WHEN COALESCE(wa.actress_cnt, 0) > 0 THEN 1 ELSE 0 END AS f_actress,
    CASE WHEN COALESCE(wo.actor_cnt, 0) > 0 THEN 1 ELSE 0 END AS f_actor,
    CASE WHEN TRIM(COALESCE(w.director, '')) <> '' THEN 1 ELSE 0 END AS f_director,
    CASE WHEN TRIM(COALESCE(w.release_date, '')) <> '' THEN 1 ELSE 0 END AS f_release_date,
    CASE
        WHEN CAST(COALESCE(NULLIF(TRIM(COALESCE(w.runtime, '')), ''), '0') AS INTEGER) > 0
            THEN 1
        ELSE 0
    END AS f_runtime,
    CASE WHEN COALESCE(wt.tag_cnt, 0) > 0 THEN 1 ELSE 0 END AS f_tag,
    CASE WHEN TRIM(COALESCE(w.cn_title, '')) <> '' THEN 1 ELSE 0 END AS f_cn_title,
    CASE WHEN TRIM(COALESCE(w.jp_title, '')) <> '' THEN 1 ELSE 0 END AS f_jp_title,
    CASE WHEN TRIM(COALESCE(w.cn_story, '')) <> '' THEN 1 ELSE 0 END AS f_cn_story,
    CASE WHEN TRIM(COALESCE(w.jp_story, '')) <> '' THEN 1 ELSE 0 END AS f_jp_story,
    CASE WHEN COALESCE(w.maker_id, 0) > 0 THEN 1 ELSE 0 END AS f_maker,
    CASE WHEN COALESCE(w.label_id, 0) > 0 THEN 1 ELSE 0 END AS f_label,
    CASE WHEN COALESCE(w.series_id, 0) > 0 THEN 1 ELSE 0 END AS f_series,
    CASE
        WHEN json_valid(COALESCE(w.fanart, ''))
             AND json_type(w.fanart) = 'array'
             AND json_array_length(w.fanart) > 0
            THEN 1
        ELSE 0
    END AS f_fanart
FROM work w
LEFT JOIN (
    SELECT work_id, COUNT(1) AS actress_cnt
    FROM work_actress_relation
    GROUP BY work_id
) wa ON wa.work_id = w.work_id
LEFT JOIN (
    SELECT work_id, COUNT(1) AS actor_cnt
    FROM work_actor_relation
    GROUP BY work_id
) wo ON wo.work_id = w.work_id
LEFT JOIN (
    SELECT work_id, COUNT(1) AS tag_cnt
    FROM work_tag_relation
    GROUP BY work_id
) wt ON wt.work_id = w.work_id
)
SELECT --水平计算表，然后统一合并
    w.work_id,
    w.serial_number AS serial_number,
    w.director AS director,
    w.release_date AS release_date,
    w.runtime AS runtime,
    (SELECT actress_list FROM actress_list WHERE work_id=w.work_id)AS actress,
    (SELECT avg_age_at_release FROM average_age_per_work WHERE work_id=w.work_id)AS avg_age,
    (SELECT actor_list FROM actor_list WHERE work_id=w.work_id)AS actor,
    (SELECT tag_list FROM tag_list WHERE work_id=w.work_id)AS tag,
    w.notes AS notes,
    w.image_url,
    w.video_url,
    w.cn_title,
    w.cn_story,
    w.jp_title,
    w.jp_story,
    (SELECT maker_name FROM maker_label_series_list WHERE work_id=w.work_id) AS maker,
    (SELECT label_name FROM maker_label_series_list WHERE work_id=w.work_id) AS label,
    (SELECT series_name FROM maker_label_series_list WHERE work_id=w.work_id) AS series,
    (
        CAST(c.f_cover AS TEXT)
        || CAST(c.f_actress AS TEXT)
        || CAST(c.f_actor AS TEXT)
        || CAST(c.f_director AS TEXT)
        || CAST(c.f_release_date AS TEXT)
        || CAST(c.f_runtime AS TEXT)
        || CAST(c.f_tag AS TEXT)
        || CAST(c.f_cn_title AS TEXT)
        || CAST(c.f_jp_title AS TEXT)
        || CAST(c.f_cn_story AS TEXT)
        || CAST(c.f_jp_story AS TEXT)
        || CAST(c.f_maker AS TEXT)
        || CAST(c.f_label AS TEXT)
        || CAST(c.f_series AS TEXT)
        || CAST(c.f_fanart AS TEXT)
    ) AS completeness_bits,
    (
        c.f_cover + c.f_actress + c.f_actor + c.f_director + c.f_release_date
        + c.f_runtime + c.f_tag + c.f_cn_title + c.f_jp_title + c.f_cn_story
        + c.f_jp_story + c.f_maker + c.f_label + c.f_series + c.f_fanart
    ) AS completeness_score
FROM 
    work w
LEFT JOIN work_completeness_flags c ON c.work_id = w.work_id;