-- Adds the Smart IV specific channels to the vitals history so each metric can
-- be charted on its own in the bed detail view.
--
-- Before this, vital_samples only kept spo2/heart_rate/temperature/drip_rate,
-- so the flow ratio, bag weight and raw drop rate existed ONLY as the current
-- live value - there was no history to plot at all. The doctor's targets are
-- stored alongside because drip_rate/flow_rate are RATIOS: without knowing the
-- target that was in force at that moment, an old "50%" cannot be turned back
-- into a real drops/min or ml/h figure.
--
-- Safe to re-run: every ADD COLUMN is guarded by an information_schema check,
-- because MySQL has no `ADD COLUMN IF NOT EXISTS`.

DELIMITER //

DROP PROCEDURE IF EXISTS add_vital_sample_columns //
CREATE PROCEDURE add_vital_sample_columns()
BEGIN
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'flow_rate') THEN
    ALTER TABLE vital_samples ADD COLUMN flow_rate INT NULL AFTER drip_rate;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'weight_g') THEN
    ALTER TABLE vital_samples ADD COLUMN weight_g INT NULL AFTER flow_rate;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'drops_per_min') THEN
    ALTER TABLE vital_samples ADD COLUMN drops_per_min INT NULL AFTER weight_g;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'target_flow_ml_h') THEN
    ALTER TABLE vital_samples ADD COLUMN target_flow_ml_h INT NULL AFTER drops_per_min;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'target_drops_per_min') THEN
    ALTER TABLE vital_samples ADD COLUMN target_drops_per_min INT NULL AFTER target_flow_ml_h;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'line_blocked') THEN
    ALTER TABLE vital_samples ADD COLUMN line_blocked TINYINT(1) NOT NULL DEFAULT 0 AFTER target_drops_per_min;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'vital_samples'
                   AND COLUMN_NAME = 'ae_alarm') THEN
    ALTER TABLE vital_samples ADD COLUMN ae_alarm TINYINT(1) NOT NULL DEFAULT 0 AFTER line_blocked;
  END IF;
END //

DELIMITER ;

CALL add_vital_sample_columns();
DROP PROCEDURE add_vital_sample_columns;
