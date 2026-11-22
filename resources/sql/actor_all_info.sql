SELECT
    a.actor_id AS actor_id,
    (
        SELECT cn FROM actor_name
        WHERE actor_id = a.actor_id AND name_type = 1
    ) AS name_cn,
    (
        SELECT jp FROM actor_name
        WHERE actor_id = a.actor_id AND name_type = 1
    ) AS name_jp,
    (
        SELECT en FROM actor_name
        WHERE actor_id = a.actor_id AND name_type = 1
    ) AS name_en,
    (
        SELECT kana FROM actor_name
        WHERE actor_id = a.actor_id AND name_type = 1
    ) AS name_kana,
    a.birthday AS birthday,
    a.height AS height,
    a.handsome AS handsome,
    a.fat AS fat,
    a.notes AS notes,
    a.image_url AS image_url,
    COUNT(DISTINCT w.work_id) AS work_count,
    MIN(w.release_date) AS first_release_date,
    MAX(w.release_date) AS latest_release_date
FROM actor a
LEFT JOIN work_actor_relation war ON a.actor_id = war.actor_id
LEFT JOIN work w
    ON war.work_id = w.work_id
    AND COALESCE(w.is_deleted, 0) = 0
GROUP BY a.actor_id
ORDER BY work_count DESC, name_cn;
