-- Widen the alert message columns.
--
-- AI v2 messages name the CAUSE, not just the fact: "IV line blocked - drops
-- have slowed and the bag is not getting lighter" instead of "IV line blocked".
-- That is the point of the change - a nurse can act on the first and not on the
-- second - but it makes messages considerably longer, and a level-3 alert
-- concatenates several of them.
--
-- VARCHAR(255) was not enough, and the failure mode was the dangerous kind:
-- MySQL rejected the INSERT and the whole alert vanished, so the single most
-- serious alarm the system can raise (line AND patient failing together, the
-- longest message) was the one most likely to be lost.
--
-- VitalsStatusEvaluator.Fit() now truncates defensively as well. Both are
-- deliberate: the truncation guarantees an alert is never lost even if this
-- migration has not been applied, and the wider column means it rarely has to.

ALTER TABLE alerts MODIFY COLUMN message VARCHAR(512) NOT NULL;
ALTER TABLE beds   MODIFY COLUMN alert_message VARCHAR(512) NULL;
