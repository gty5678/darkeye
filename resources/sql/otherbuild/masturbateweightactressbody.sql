--按撸管次数加权计算平均女优身材
WITH cup_numeric AS (--预计算罩杯转换
SELECT
a.actress_id,
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
WHERE a.cup IS NOT NULL
),
weighted_stats as(--加权后的数值
SELECT
    w.work_id,
	w.serial_number,
	AVG(a.height)*w.masturbate AS weighted_height,
	AVG(a.bust)*w.masturbate AS weighted_bust,
	AVG(a.waist)*w.masturbate AS weighted_waist,
	AVG(a.hip)*w.masturbate AS weighted_hip,
	AVG(cup_num)*w.masturbate AS weighted_cup,
	(round((julianday(w.release_date) - julianday(a.birthday)) / 365.25,1)-0.25)*w.masturbate AS weighted_age, --计算平均拍摄年龄
	w.masturbate
FROM work w
JOIN work_actress_relation war ON war.work_id=w.work_id
JOIN actress a ON a.actress_id=war.actress_id
JOIN cup_numeric cn ON cn.actress_id=a.actress_id
WHERE w.release_date IS NOT NULL AND a.birthday IS NOT NULL AND NOT w.masturbate=0
GROUP BY w.work_id
)
SELECT 
	ROUND(SUM(weighted_height) * 1.0 / SUM(masturbate), 1) AS avg_height,
	ROUND(SUM(weighted_bust) * 1.0 / SUM(masturbate), 1) AS avg_bust,
	ROUND(SUM(weighted_waist) * 1.0 / SUM(masturbate), 1) AS avg_waist,
	ROUND(SUM(weighted_hip) * 1.0 / SUM(masturbate), 1) AS avg_hip,
	ROUND(SUM(weighted_cup) * 1.0 / SUM(masturbate), 1) AS avg_cup,
	ROUND(SUM(weighted_age) * 1.0 / SUM(masturbate), 1) AS avg_age
FROM weighted_stats
