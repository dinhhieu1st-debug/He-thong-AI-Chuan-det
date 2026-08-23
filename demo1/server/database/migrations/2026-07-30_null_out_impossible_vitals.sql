-- Backfill: turn physiologically impossible ZEROS into NULL in the vitals history.
--
-- Rows written before 2026-07-29 stored 0 for a channel whose signal was lost,
-- because BedDataParser defaults a missing JSON field to 0 and the old
-- VitalSampleRepository persisted that verbatim. On a chart that reads as a
-- patient with no pulse / no oxygen saturation, rather than a detached sensor.
-- The repository now stores NULL for a lost channel (so the chart draws a gap);
-- this fixes the rows already on disk.
--
-- ONLY heart_rate and spo2 are touched. 0 is impossible for those in a living
-- patient, so it is unambiguously a sentinel. It is NOT applied to:
--   flow_rate / drip_rate  - 0% is a REAL reading (fully occluded line)
--   drops_per_min          - 0 is a REAL reading (infusion stopped)
--   weight_g               - 0 is a REAL reading (empty / freshly tared scale)
-- Nulling those would erase exactly the occlusion evidence a doctor needs.

UPDATE vital_samples SET heart_rate = NULL WHERE heart_rate = 0;
UPDATE vital_samples SET spo2       = NULL WHERE spo2       = 0;
