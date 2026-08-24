-- Demo seed data: a few rooms, each with its own gateway device and several
-- beds, so the UI isn't empty before real hardware is deployed everywhere.
--
-- Matches the intended production topology: 1 gateway per room, 1 XG26
-- end-device chip per bed reporting over Zigbee to that room's gateway.
--
-- Usage: mysql -u <user> -p his_server < database/seed_demo.sql
--
-- Safe to re-run: uses INSERT ... ON DUPLICATE KEY UPDATE.

USE his_server;

-- Remove the old ad-hoc single demo bed from earlier testing, if present.
DELETE FROM beds WHERE bed_id = 'BED-01';

-- --- Rooms & gateways -------------------------------------------------

INSERT INTO devices (device_id, device_type, assigned_bed_id, room, status)
VALUES
  ('GW-ICU1',  'GATEWAY', NULL, 'ICU-1',  'PENDING'),
  ('GW-ICU2',  'GATEWAY', NULL, 'ICU-2',  'PENDING'),
  ('GW-WARDA', 'GATEWAY', NULL, 'Ward-A', 'PENDING')
ON DUPLICATE KEY UPDATE
  device_type = VALUES(device_type),
  room = VALUES(room),
  status = VALUES(status);

-- --- Beds (all Offline: no live end-device reporting to them yet, except
-- BED-101 which the real sensor firmware/gateway reports to) ----------

INSERT INTO beds (bed_id, room, status)
VALUES
  ('BED-101', 'ICU-1',  'OFFLINE'),
  ('BED-102', 'ICU-1',  'OFFLINE'),
  ('BED-103', 'ICU-1',  'OFFLINE'),
  ('BED-201', 'ICU-2',  'OFFLINE'),
  ('BED-202', 'ICU-2',  'OFFLINE'),
  ('BED-203', 'ICU-2',  'OFFLINE'),
  ('BED-301', 'Ward-A', 'OFFLINE'),
  ('BED-302', 'Ward-A', 'OFFLINE')
ON DUPLICATE KEY UPDATE
  room = VALUES(room);

-- --- One XG26 end-device chip per bed ---------------------------------

INSERT INTO devices (device_id, device_type, assigned_bed_id, room, status)
VALUES
  ('XG26-BED-101', 'XG26', 'BED-101', 'ICU-1',  'PENDING'),
  ('XG26-BED-102', 'XG26', 'BED-102', 'ICU-1',  'PENDING'),
  ('XG26-BED-103', 'XG26', 'BED-103', 'ICU-1',  'PENDING'),
  ('XG26-BED-201', 'XG26', 'BED-201', 'ICU-2',  'PENDING'),
  ('XG26-BED-202', 'XG26', 'BED-202', 'ICU-2',  'PENDING'),
  ('XG26-BED-203', 'XG26', 'BED-203', 'ICU-2',  'PENDING'),
  ('XG26-BED-301', 'XG26', 'BED-301', 'Ward-A', 'PENDING'),
  ('XG26-BED-302', 'XG26', 'BED-302', 'Ward-A', 'PENDING')
ON DUPLICATE KEY UPDATE
  assigned_bed_id = VALUES(assigned_bed_id),
  room = VALUES(room);

SELECT 'demo seed data loaded' AS status;
