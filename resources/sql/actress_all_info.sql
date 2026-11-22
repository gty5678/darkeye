SELECT
    a.actress_id AS actress_id,
    (
        SELECT cn FROM actress_name
        WHERE actress_id = a.actress_id AND name_type = 1
    ) AS name_cn,
    (
        SELECT jp FROM actress_name
        WHERE actress_id = a.actress_id AND name_type = 1
    ) AS name_jp,
    (
        SELECT en FROM actress_name
        WHERE actress_id = a.actress_id AND name_type = 1
    ) AS name_en,
    (
        SELECT kana FROM actress_name
        WHERE actress_id = a.actress_id AND name_type = 1
    ) AS name_kana,
    (
        SELECT GROUP_CONCAT(cn, ',')
        FROM actress_name
        WHERE actress_id = a.actress_id AND name_type IN (3, 4)
    ) AS alias_cn,
    a.birthday AS birthday,
    a.height AS height,
    a.bust AS bust,
    a.waist AS waist,
    a.hip AS hip,
    a.cup AS cup,
    a.debut_date AS debut_date,
    CASE
        WHEN a.debut_date IS NOT NULL AND a.birthday IS NOT NULL THEN
            ROUND(
                (julianday(a.debut_date) - julianday(a.birthday)) / 365.25,
                1
            ) - 0.25
        ELSE NULL
    END AS debut_age,
    COUNT(DISTINCT w.work_id) AS work_count,
    MIN(w.release_date) AS first_release_date,
    MAX(w.release_date) AS latest_release_date,
    a.notes AS notes,
    a.image_urlA AS image_urlA,
    a.image_urlB AS image_urlB,
    a.minnano_url AS minnano_url,
    a.need_update AS need_update
FROM actress a
LEFT JOIN work_actress_relation war ON a.actress_id = war.actress_id
LEFT JOIN work w
    ON war.work_id = w.work_id
    AND COALESCE(w.is_deleted, 0) = 0
GROUP BY a.actress_id
ORDER BY work_count DESC, name_cn;
