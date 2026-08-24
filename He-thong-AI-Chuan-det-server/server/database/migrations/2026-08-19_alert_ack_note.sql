-- Lets whoever acknowledges an alert record what they actually did about it.
--
-- Before this, acknowledging only recorded WHO and WHEN - useful for an audit
-- trail, useless for a nurse re-reading the alert later trying to remember
-- whether the line was re-clipped or a doctor was called. The note is
-- optional: acknowledging must still work with one click when there is
-- nothing worth writing down.
--
-- Safe to re-run: guarded by an information_schema check, because MySQL has
-- no `ADD COLUMN IF NOT EXISTS`.

DELIMITER //

DROP PROCEDURE IF EXISTS add_alert_ack_note //
CREATE PROCEDURE add_alert_ack_note()
BEGIN
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                 WHERE TABLE_SCHEMA = DATABASE()
                   AND TABLE_NAME = 'alerts'
                   AND COLUMN_NAME = 'acknowledgement_note') THEN
    ALTER TABLE alerts ADD COLUMN acknowledgement_note VARCHAR(500) NULL AFTER acknowledged_by;
  END IF;
END //

DELIMITER ;

CALL add_alert_ack_note();
DROP PROCEDURE add_alert_ack_note;
