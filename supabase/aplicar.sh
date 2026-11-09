#!/usr/bin/env bash
# T-11: aplica las migraciones pendientes, los datos de prueba y las tablas IP-to-Country.
# Usa el mismo registro que la CLI de Supabase (supabase_migrations.schema_migrations), así que
# se puede repetir y también funciona si alguien usa `supabase db push` después.
# Uso: aplicar.sh <cadena de conexión>   (necesita psql; `just db` lo corre dentro de Docker)
set -euo pipefail

url="${1:?Uso: aplicar.sh <cadena de conexión>}"
dir="$(cd "$(dirname "$0")" && pwd)"
sql() { psql "$url" --no-psqlrc -v ON_ERROR_STOP=1 -q -t -A "$@"; }

sql -c "create schema if not exists supabase_migrations;
        create table if not exists supabase_migrations.schema_migrations (
          version text primary key, statements text[], name text);"

for f in "$dir"/migrations/*.sql; do
  base="$(basename "$f" .sql)"
  version="${base%%_*}"
  if [ "$(sql -c "select count(*) from supabase_migrations.schema_migrations where version = '$version'")" != 0 ]; then
    echo "ya aplicada: $base"
    continue
  fi
  echo "aplicando:   $base"
  sql --single-transaction -f "$f" \
      -c "insert into supabase_migrations.schema_migrations (version, name) values ('$version', '${base#*_}')"
done

echo "datos de prueba: seed.sql"
sql --single-transaction -f "$dir/seed.sql"

echo "IP-to-Country"
bash "$dir/seed/ip_to_country.sh" "$url"
