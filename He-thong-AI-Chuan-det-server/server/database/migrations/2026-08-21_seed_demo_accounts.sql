-- Demo accounts for filming (video-demo branch only — do not merge to main).
--
-- Run once against the DB used for filming:
--   docker exec -i his-mysql mysql -uroot -pdemopass his_server \
--     < server/database/migrations/2026-08-21_seed_demo_accounts.sql
--
-- must_change_password is FALSE here (unlike the admin seed in
-- 2026-08-17_users_roles_assignments.sql) so the login flow used on camera
-- never gets interrupted by a forced password-change screen mid-take.
--
--   username: yta       password: YTaDemo@2026      role: NURSE
--   username: kythuat   password: KyThuat@2026      role: TECHNICIAN

INSERT INTO users (username, password_hash, full_name, role, must_change_password)
SELECT 'yta',
       'pbkdf2$210000$Ib6Rg6mB0Zb6AyZwmO+l8g==$BTlovEMTPZ3za4UmEASMPz3AFcrl4FiNzojn0RzeKn4=',
       'Y Ta Demo', 'NURSE', FALSE
WHERE NOT EXISTS (SELECT 1 FROM users WHERE username = 'yta');

INSERT INTO users (username, password_hash, full_name, role, must_change_password)
SELECT 'kythuat',
       'pbkdf2$210000$vvNYgOdxx+Jy7HmfDju19A==$Q+9Zm+DuK84+YVymdOa+d41DtfyrpEK2anD8MOerhdI=',
       'Ky Thuat Demo', 'TECHNICIAN', FALSE
WHERE NOT EXISTS (SELECT 1 FROM users WHERE username = 'kythuat');
