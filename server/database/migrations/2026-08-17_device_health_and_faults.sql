-- Device health, a device-only event log, and nurse-raised fault reports.
--
-- Run once against an existing database:
--   docker exec -i his-mysql mysql -uroot -pdevpass his_server \
--     < server/database/migrations/2026-08-17_device_health_and_faults.sql
--
-- MySQL 8.0 has no "ADD COLUMN IF NOT EXISTS", so the ALTERs are guarded by
-- hand and the file is safe to re-run.

-- What a nurse reports and a technician acts on.
--
-- device_id is nullable on purpose: the nurse reports a BED ("the SpO2 clip on
-- 101 reads nothing"), not a hardware address they have no reason to know. The
-- server fills in whichever device is assigned to that bed - and a bed with no
-- device assigned must still be reportable, because "there is no working
-- device here" is exactly the kind of thing worth reporting.
CREATE TABLE IF NOT EXISTS device_fault_reports (
  report_id       BIGINT AUTO_INCREMENT PRIMARY KEY,
  bed_id          VARCHAR(50)  NOT NULL,
  device_id       VARCHAR(80)  NULL,
  channel         ENUM('HR', 'SPO2', 'FLOW', 'DROPS', 'OTHER') NOT NULL DEFAULT 'OTHER',
  note            VARCHAR(500) NOT NULL DEFAULT '',
  reported_by     VARCHAR(64)  NOT NULL,
  reported_at     DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  status          ENUM('OPEN', 'IN_PROGRESS', 'RESOLVED') NOT NULL DEFAULT 'OPEN',
  handled_by      VARCHAR(64)  NULL,
  handled_at      DATETIME(3)  NULL,
  resolution_note VARCHAR(500) NULL,
  KEY idx_fault_status (status, reported_at),
  KEY idx_fault_bed (bed_id, reported_at)
) ENGINE=InnoDB;

-- The technician's log.
--
-- Deliberately has no column that could hold a vital sign. The existing
-- System Log answers "what happened to this patient" and embeds SpO2/HR in
-- every row; this one answers "what happened to this equipment", and a
-- technician needs the second question only.
--
-- Rows are written on STATE CHANGE, never per reading - the same reason
-- AlertTransitionTracker only writes when a bed enters a bad state. At one
-- reading per second per bed, logging every packet would bury the three lines
-- that matter under 86,400 that do not.
CREATE TABLE IF NOT EXISTS device_events (
  event_id    BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id   VARCHAR(80)  NOT NULL,
  bed_id      VARCHAR(50)  NULL,
  event_type  ENUM('JOINED', 'ASSIGNED', 'ONLINE', 'SENSOR_FAULT', 'OFFLINE',
                   'FAULT_REPORTED', 'FAULT_RESOLVED') NOT NULL,
  detail      VARCHAR(255) NULL,
  occurred_at DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  KEY idx_device_events (device_id, occurred_at),
  KEY idx_device_events_time (occurred_at)
) ENGINE=InnoDB;

-- Device health, derived from the live data stream rather than typed in.
--   link_quality  : Zigbee signal, already present in the MQTT payload and
--                   until now thrown away. It is what tells a technician
--                   whether to bring a repeater or a replacement sensor.
--   channels_lost : which channels have gone quiet, e.g. "SPO2,FLOW".
--   last_data_at  : last reading actually received from this device.
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
           WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'devices'
             AND COLUMN_NAME = 'link_quality');
SET @s := IF(@c = 0,
  'ALTER TABLE devices
     ADD COLUMN link_quality  INT         NULL,
     ADD COLUMN channels_lost VARCHAR(64) NULL,
     ADD COLUMN last_data_at  DATETIME(3) NULL',
  'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- SENSOR_FAULT is a state a device can be in: reporting, but with a channel
-- that has been silent long enough to mean broken rather than briefly noisy.
-- It is separate from OFFLINE because the two send a technician out with
-- different equipment.
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
           WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'devices'
             AND COLUMN_NAME = 'status'
             AND COLUMN_TYPE LIKE '%SENSOR_FAULT%');
SET @s := IF(@c = 0,
  'ALTER TABLE devices MODIFY COLUMN status
     ENUM(''ONLINE'', ''PENDING'', ''WARNING'', ''OFFLINE'', ''SENSOR_FAULT'')
     NOT NULL DEFAULT ''PENDING''',
  'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;
