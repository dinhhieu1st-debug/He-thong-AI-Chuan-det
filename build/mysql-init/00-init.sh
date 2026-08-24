#!/bin/bash
# Loads the real schema, then every migration in name order, then optionally the
# demo seed.
#
# A shell script rather than mounting the .sql files straight into
# /docker-entrypoint-initdb.d: that directory runs files alphabetically, which
# would put the dated migrations before schema.sql and fail. Order matters here
# and this is the only way to state it.
#
# Runs on FIRST START ONLY - MySQL skips this directory when the data volume
# already has a database in it. To load it again, destroy the volume:
#   docker compose -f build/docker-compose.dev.yml down -v
set -euo pipefail

DB=/opt/his/database

echo "[init] schema"
mysql --protocol=socket -uroot < "$DB/schema.sql"

echo "[init] migrations"
for f in $(ls "$DB"/migrations/*.sql | sort); do
  echo "[init]   $(basename "$f")"
  mysql --protocol=socket -uroot his_server < "$f"
done

# The demo seed invents 8 beds and a placeholder device per bed. Useful against
# the simulator, actively confusing against real hardware: the placeholders sit
# in the Devices tab next to the one real chip, all claiming beds that nothing
# reports to. Set LOAD_DEMO_SEED=0 when a real gateway is going to connect.
if [ "${LOAD_DEMO_SEED:-1}" = "0" ]; then
  echo "[init] demo seed SKIPPED (LOAD_DEMO_SEED=0) - no beds, no devices"
  echo "[init] create beds as admin, then assign the real device to one as technician"
else
  echo "[init] demo seed (8 beds across 3 rooms, one XG26 per bed)"
  mysql --protocol=socket -uroot his_server < "$DB/seed_demo.sql"
fi

echo "[init] done"
