--计算加权平均的女优身材数据，按片的数量加权
--这个也不太对，应该按照撸的次数加权
WITH actress_weights AS (
  SELECT
    wa.actress_id,
    COUNT(*) AS work_count
  FROM work_actress_relation wa
  GROUP BY wa.actress_id
),
cup_numeric AS (
  SELECT
    a.actress_id,
    aw.work_count,
    CASE UPPER(a.cup)
      WHEN 'A' THEN 1
      WHEN 'B' THEN 2
      WHEN 'C' THEN 3
      WHEN 'D' THEN 4
      WHEN 'E' THEN 5
      WHEN 'F' THEN 6
      WHEN 'G' THEN 7
      WHEN 'H' THEN 8
      WHEN 'I' THEN 9
      WHEN 'J' THEN 10
      ELSE NULL
    END AS cup_num
  FROM actress a
  JOIN actress_weights aw ON a.actress_id = aw.actress_id
  WHERE a.cup IS NOT NULL
),
weighted_stats AS (
  SELECT
    a.actress_id,
    aw.work_count,
    a.height * aw.work_count AS weighted_height,
    a.bust * aw.work_count AS weighted_bust,
    a.waist * aw.work_count AS weighted_waist,
    a.hip * aw.work_count AS weighted_hip,
	cn.cup_num * aw.work_count AS weighted_cup
  FROM actress a
  JOIN actress_weights aw ON a.actress_id = aw.actress_id
  JOIN cup_numeric cn ON cn.actress_id=a.actress_id
)
SELECT
  ROUND(SUM(weighted_cup) * 1.0 / SUM(work_count), 1) AS avg_cup,
  ROUND(SUM(weighted_height) * 1.0 / SUM(work_count), 1) AS avg_height,
  ROUND(SUM(weighted_bust) * 1.0 / SUM(work_count), 1) AS avg_bust,
  ROUND(SUM(weighted_waist) * 1.0 / SUM(work_count), 1) AS avg_waist,
  ROUND(SUM(weighted_hip) * 1.0 / SUM(work_count), 1) AS avg_hip
FROM weighted_stats;
