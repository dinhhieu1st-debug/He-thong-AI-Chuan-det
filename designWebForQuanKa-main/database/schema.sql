-- HIS Server MySQL schema.
-- Replaces the old fpt_hospital_monitoring.sql (Vietnamese enum values retired;
-- fresh rewrite, no production data to migrate).
--
-- Usage: mysql -u <user> -p < database/schema.sql

CREATE DATABASE IF NOT EXISTS his_server
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE his_server;

-- Current-state source of truth for each bed. Did not exist in the old app
-- (bed "current state" only ever lived in an in-memory dictionary).
CREATE TABLE IF NOT EXISTS beds (
  bed_id VARCHAR(50) NOT NULL PRIMARY KEY,
  room VARCHAR(100) NOT NULL DEFAULT '',
  status ENUM('STABLE', 'WARNING', 'CRITICAL', 'OFFLINE') NOT NULL DEFAULT 'OFFLINE',
  spo2 INT NULL,
  heart_rate INT NULL,
  temperature DECIMAL(4,1) NULL,
  drip_rate INT NULL,
  flow_rate INT NULL,
  heart_rate_signal BOOLEAN NOT NULL DEFAULT TRUE,
  spo2_signal BOOLEAN NOT NULL DEFAULT TRUE,
  flow_signal BOOLEAN NOT NULL DEFAULT TRUE,
  drip_rate_signal BOOLEAN NOT NULL DEFAULT TRUE,
  line_blocked BOOLEAN NOT NULL DEFAULT FALSE,
  ae_alarm BOOLEAN NOT NULL DEFAULT FALSE,
  weight_g INT NULL,
  drops_per_min INT NULL,
  target_flow_ml_h INT NULL,
  target_drops_per_min INT NULL,
  tare_in_progress BOOLEAN NOT NULL DEFAULT FALSE,
  tare_just_completed BOOLEAN NOT NULL DEFAULT FALSE,
  hr_baseline_just_completed BOOLEAN NOT NULL DEFAULT FALSE,
  hr_baseline_seconds_remaining INT NULL,
  hr_baseline_bpm INT NULL,
  hr_baseline_captured_at DATETIME(3) NULL,
  last_tare_completed_at DATETIME(3) NULL,
  last_seen_tare_event_count INT NULL,
  last_seen_hr_baseline_event_count INT NULL,
  alert_message VARCHAR(255) NULL,
  device_id VARCHAR(80) NULL,
  last_data_at DATETIME(3) NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_beds_status (status),
  KEY idx_beds_room (room)
) ENGINE=InnoDB;

-- If you already ran an older version of this schema (before flow_rate/*_signal/
-- line_blocked/ae_alarm existed), add the missing columns manually (MySQL has
-- no ADD COLUMN IF NOT EXISTS), e.g.:
--   ALTER TABLE beds ADD COLUMN flow_rate INT NULL;
--   ALTER TABLE beds ADD COLUMN heart_rate_signal BOOLEAN NOT NULL DEFAULT TRUE;
--   ALTER TABLE beds ADD COLUMN spo2_signal BOOLEAN NOT NULL DEFAULT TRUE;
--   ALTER TABLE beds ADD COLUMN flow_signal BOOLEAN NOT NULL DEFAULT TRUE;
--   ALTER TABLE beds ADD COLUMN drip_rate_signal BOOLEAN NOT NULL DEFAULT TRUE;
--   ALTER TABLE beds ADD COLUMN line_blocked BOOLEAN NOT NULL DEFAULT FALSE;
--   ALTER TABLE beds ADD COLUMN ae_alarm BOOLEAN NOT NULL DEFAULT FALSE;
--
-- Added alongside the doctor-settable target infusion rate + loadcell tare
-- button + HR baseline indication features:
--   ALTER TABLE beds ADD COLUMN weight_g INT NULL;
--   ALTER TABLE beds ADD COLUMN drops_per_min INT NULL;
--   ALTER TABLE beds ADD COLUMN target_flow_ml_h INT NULL;
--   ALTER TABLE beds ADD COLUMN target_drops_per_min INT NULL;
--   ALTER TABLE beds ADD COLUMN tare_in_progress BOOLEAN NOT NULL DEFAULT FALSE;
--   ALTER TABLE beds ADD COLUMN tare_just_completed BOOLEAN NOT NULL DEFAULT FALSE;
--   ALTER TABLE beds ADD COLUMN hr_baseline_just_completed BOOLEAN NOT NULL DEFAULT FALSE;
--
-- Added alongside the HR countdown + persistent tare/baseline timestamp features:
--   ALTER TABLE beds ADD COLUMN hr_baseline_seconds_remaining INT NULL;
--   ALTER TABLE beds ADD COLUMN hr_baseline_captured_at DATETIME(3) NULL;
--   ALTER TABLE beds ADD COLUMN last_tare_completed_at DATETIME(3) NULL;
--
-- Added alongside the persistent event-counter fix for the tare/baseline
-- completion race (a one-shot bit can be missed if it fires before Zigbee
-- reporting is configured; a persistent count that only ever goes up cannot):
--   ALTER TABLE beds ADD COLUMN hr_baseline_bpm INT NULL;
--   ALTER TABLE beds ADD COLUMN last_seen_tare_event_count INT NULL;
--   ALTER TABLE beds ADD COLUMN last_seen_hr_baseline_event_count INT NULL;

-- Real device registry. The old app's device screen was 100% hardcoded mock
-- data with no live wiring at all; this is the first real implementation.
CREATE TABLE IF NOT EXISTS devices (
  device_id VARCHAR(80) NOT NULL PRIMARY KEY,
  device_type ENUM('XG26', 'GATEWAY', 'BLE') NOT NULL DEFAULT 'XG26',
  assigned_bed_id VARCHAR(50) NULL,
  room VARCHAR(100) NULL,
  status ENUM('ONLINE', 'PENDING', 'WARNING', 'OFFLINE') NOT NULL DEFAULT 'PENDING',
  battery_percent INT NULL,
  rssi INT NULL,
  eui64 VARCHAR(32) NULL,
  last_seen_at DATETIME(3) NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_devices_assigned_bed (assigned_bed_id),
  KEY idx_devices_status (status)
) ENGINE=InnoDB;

-- Time-series vitals history (kept from the old schema; enum renamed to English).
CREATE TABLE IF NOT EXISTS vital_samples (
  sample_id BIGINT AUTO_INCREMENT PRIMARY KEY,
  bed_id VARCHAR(50) NOT NULL,
  device_id VARCHAR(80) NULL,
  spo2 INT NULL,
  heart_rate INT NULL,
  temperature DECIMAL(4,1) NULL,
  drip_rate INT NULL,
  status ENUM('STABLE', 'WARNING', 'CRITICAL') NOT NULL DEFAULT 'STABLE',
  recorded_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  KEY idx_vital_samples_bed_time (bed_id, recorded_at),
  KEY idx_vital_samples_device_time (device_id, recorded_at),
  KEY idx_vital_samples_time (recorded_at)
) ENGINE=InnoDB;

-- Alert history (kept from the old schema; enum renamed to English). Only
-- written on a status transition into WARNING/CRITICAL — see
-- Domain/AlertTransitionTracker.cs.
CREATE TABLE IF NOT EXISTS alerts (
  alert_id BIGINT AUTO_INCREMENT PRIMARY KEY,
  bed_id VARCHAR(50) NOT NULL,
  device_id VARCHAR(80) NULL,
  alert_level ENUM('WARNING', 'CRITICAL') NOT NULL,
  alert_type VARCHAR(100) NOT NULL,
  message VARCHAR(255) NOT NULL,
  spo2 INT NULL,
  heart_rate INT NULL,
  temperature DECIMAL(4,1) NULL,
  drip_rate INT NULL,
  acknowledged BOOLEAN NOT NULL DEFAULT FALSE,
  acknowledged_at DATETIME NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_alerts_bed_time (bed_id, created_at),
  KEY idx_alerts_ack_time (acknowledged, created_at),
  KEY idx_alerts_level_time (alert_level, created_at),
  KEY idx_alerts_time (created_at)
) ENGINE=InnoDB;

-- FCM device tokens for mobile push notifications. Replaces the old app's
-- flat-file token store (%AppData%/FPT-HIS/fcm_tokens.txt).
CREATE TABLE IF NOT EXISTS fcm_tokens (
  token_id BIGINT AUTO_INCREMENT PRIMARY KEY,
  token VARCHAR(255) NOT NULL UNIQUE,
  registered_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_used_at DATETIME NULL
) ENGINE=InnoDB;

SELECT 'his_server schema ready' AS status;
