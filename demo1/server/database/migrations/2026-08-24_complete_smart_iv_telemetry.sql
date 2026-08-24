-- Persist the complete G26 forecast, line and alert contract. Safe to re-run.
DELIMITER //
DROP PROCEDURE IF EXISTS add_complete_smart_iv_telemetry //
CREATE PROCEDURE add_complete_smart_iv_telemetry()
BEGIN
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='hr_forecast_16s') THEN ALTER TABLE vital_samples ADD COLUMN hr_forecast_16s INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='spo2_forecast_16s') THEN ALTER TABLE vital_samples ADD COLUMN spo2_forecast_16s INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='hr_trend_bpm_per_min') THEN ALTER TABLE vital_samples ADD COLUMN hr_trend_bpm_per_min INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='ts_anomaly_score_x100') THEN ALTER TABLE vital_samples ADD COLUMN ts_anomaly_score_x100 INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='drops_forecast_16s') THEN ALTER TABLE vital_samples ADD COLUMN drops_forecast_16s INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='drops_trend_dpm_per_min') THEN ALTER TABLE vital_samples ADD COLUMN drops_trend_dpm_per_min INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='drops_forecast_trusted') THEN ALTER TABLE vital_samples ADD COLUMN drops_forecast_trusted TINYINT(1) NOT NULL DEFAULT 0; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='line_state') THEN ALTER TABLE vital_samples ADD COLUMN line_state INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='remaining_ml') THEN ALTER TABLE vital_samples ADD COLUMN remaining_ml INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='remaining_min') THEN ALTER TABLE vital_samples ADD COLUMN remaining_min INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='drop_interval_ms') THEN ALTER TABLE vital_samples ADD COLUMN drop_interval_ms INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='drop_event_count') THEN ALTER TABLE vital_samples ADD COLUMN drop_event_count INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='server_drop_level') THEN ALTER TABLE vital_samples ADD COLUMN server_drop_level INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='vitals_level') THEN ALTER TABLE vital_samples ADD COLUMN vitals_level INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='alert_level') THEN ALTER TABLE vital_samples ADD COLUMN alert_level INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='vitals_test_mode') THEN ALTER TABLE vital_samples ADD COLUMN vitals_test_mode INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='ai_input_heart_rate') THEN ALTER TABLE vital_samples ADD COLUMN ai_input_heart_rate INT NULL; END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME='ai_input_spo2') THEN ALTER TABLE vital_samples ADD COLUMN ai_input_spo2 INT NULL; END IF;
END //
DELIMITER ;
CALL add_complete_smart_iv_telemetry();
DROP PROCEDURE add_complete_smart_iv_telemetry;
