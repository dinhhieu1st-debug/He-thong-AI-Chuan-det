-- Users, roles, ward assignments, and the two columns that role-awareness
-- makes necessary elsewhere.
--
-- Run once against an existing database:
--   docker exec -i his-mysql mysql -uroot -pdevpass his_server \
--     < server/database/migrations/2026-08-17_users_roles_assignments.sql
--
-- MySQL 8.0 has no "ADD COLUMN IF NOT EXISTS" (that is MariaDB only), so the
-- ALTERs below are guarded by hand - re-running the file is safe.

CREATE TABLE IF NOT EXISTS users (
  user_id              INT AUTO_INCREMENT PRIMARY KEY,
  username             VARCHAR(64)  NOT NULL,
  -- PBKDF2-HMACSHA256, salt included, iteration count stored with the hash so
  -- it can be raised later without invalidating existing passwords. A plain
  -- password is never stored, and never logged.
  password_hash        VARCHAR(255) NOT NULL,
  full_name            VARCHAR(120) NOT NULL DEFAULT '',
  role                 ENUM('NURSE', 'TECHNICIAN', 'ADMIN') NOT NULL DEFAULT 'NURSE',
  is_active            BOOLEAN      NOT NULL DEFAULT TRUE,
  -- Seeded and admin-reset accounts start with a known password, so the first
  -- login must replace it before anything else is allowed.
  must_change_password BOOLEAN      NOT NULL DEFAULT TRUE,
  created_at           DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login_at        DATETIME(3)  NULL,
  UNIQUE KEY uq_users_username (username)
) ENGINE=InnoDB;

-- Which rooms / beds a user is responsible for.
--
-- One table with a scope type rather than two tables, because a nurse is
-- normally assigned a whole ROOM but sometimes an individual bed (a patient
-- moved, a shift covering one extra bed), and both need to be expressible
-- without a second join everywhere.
--
-- IMPORTANT: this is a DUTY ROSTER, not an access filter. Every clinical user
-- still sees every bed. A nurse covering for a colleague during a code, or
-- glancing at the ward overview, must not be shown a blank screen because of a
-- roster row - that would make the system dangerous in exactly the moments it
-- exists for. What the roster drives is the "my beds" view and the profile
-- page.
CREATE TABLE IF NOT EXISTS user_assignments (
  assignment_id INT AUTO_INCREMENT PRIMARY KEY,
  user_id       INT NOT NULL,
  scope_type    ENUM('ROOM', 'BED') NOT NULL,
  scope_value   VARCHAR(100) NOT NULL,
  assigned_at   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_assignment (user_id, scope_type, scope_value),
  KEY idx_assignment_user (user_id),
  CONSTRAINT fk_assignment_user FOREIGN KEY (user_id)
    REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Who acknowledged an alert. Recording only WHEN it happened was fine while
-- everyone shared one anonymous session; with named accounts, an alert that
-- was silenced by nobody in particular is a gap in the record.
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
           WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'alerts'
             AND COLUMN_NAME = 'acknowledged_by');
SET @s := IF(@c = 0,
  'ALTER TABLE alerts ADD COLUMN acknowledged_by VARCHAR(64) NULL AFTER acknowledged_at',
  'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Who is in the bed. The nurse profile page has to answer "which patients am I
-- looking after", and until now the system had no concept of a patient at all.
-- Deliberately minimal: a name, a hospital code, and when they were admitted.
-- No diagnosis, no notes, no history - none of that is needed to monitor a
-- drip, and every extra field is more personal data to protect.
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
           WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'beds'
             AND COLUMN_NAME = 'patient_name');
SET @s := IF(@c = 0,
  'ALTER TABLE beds
     ADD COLUMN patient_name VARCHAR(120) NULL,
     ADD COLUMN patient_code VARCHAR(40)  NULL,
     ADD COLUMN admitted_at  DATETIME     NULL',
  'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- First administrator. The password below is PUBLIC (it is in this file, in
-- git), which is exactly why must_change_password is set: the account cannot
-- be used for anything until the password has been replaced at first login.
--   username: admin
--   password: ChangeMe!123
INSERT INTO users (username, password_hash, full_name, role, must_change_password)
SELECT 'admin',
       'pbkdf2$210000$9FssR0V7Ixy7auGB8flTmg==$xGiJ4QYFiJbf6RJ4C+qhhhJwWFQYp10zvyABDMDPX8s=',
       'Quan tri he thong', 'ADMIN', TRUE
WHERE NOT EXISTS (SELECT 1 FROM users WHERE username = 'admin');
