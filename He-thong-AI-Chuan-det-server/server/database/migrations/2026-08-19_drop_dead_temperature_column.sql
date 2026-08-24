-- Drops the `temperature` column from beds, vital_samples and alerts.
--
-- No device in this system has ever measured patient temperature. The column
-- is a leftover from an earlier firmware design that "borrowed" the standard
-- Zigbee Temperature Measurement cluster as one of five endpoints carrying
-- vitals (see the history comment in firmware/app.c) - that design was
-- replaced by a custom cluster whose five real attributes are HeartRate,
-- Spo2, FlowRatio, DropRatio and AlarmBitmap. Temperature was dropped from
-- the wire format, but the DB column, C# model fields and (briefly) a UI
-- tile survived it, defaulting to 0 with no real sensor behind that number -
-- which reads as an actual (very wrong) reading rather than "not measured".
--
-- Safe to re-run: every DROP COLUMN is guarded by an information_schema
-- check, because MySQL has no `DROP COLUMN IF EXISTS`.

DELIMITER //

DROP PROCEDURE IF EXISTS drop_dead_temperature_column //
CREATE PROCEDURE drop_dead_temperature_column()
BEGIN
  IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'beds'
               AND COLUMN_NAME = 'temperature') THEN
    ALTER TABLE beds DROP COLUMN temperature;
  END IF;

  IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'vital_samples'
               AND COLUMN_NAME = 'temperature') THEN
    ALTER TABLE vital_samples DROP COLUMN temperature;
  END IF;

  IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'alerts'
               AND COLUMN_NAME = 'temperature') THEN
    ALTER TABLE alerts DROP COLUMN temperature;
  END IF;
END //

DELIMITER ;

CALL drop_dead_temperature_column();
DROP PROCEDURE drop_dead_temperature_column;
