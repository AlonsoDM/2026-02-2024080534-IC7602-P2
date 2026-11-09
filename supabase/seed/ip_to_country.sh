#!/usr/bin/env bash
# T-11: carga las tablas IP-to-Country (countries e ip_to_country) con los CSV del P1
# Uso: ip_to_country.sh <cadena de conexión>
set -euo pipefail

url="${1:?Uso: ip_to_country.sh <cadena de conexión>}"
dir="$(cd "$(dirname "$0")" && pwd)"

{
  cat <<'SQL'
\set ON_ERROR_STOP on
begin;
create temp table countries_csv (country text, latitude text, longitude text, name text) on commit drop;
create temp table dbip_csv (ip_start text, ip_end text, country_code text) on commit drop;
\copy countries_csv from stdin with (format csv, header true)
SQL
  cat "$dir/countries.csv"
  printf '\\.\n'
  echo '\copy dbip_csv from stdin with (format csv, header true)'
  gzip -dc "$dir/dbip-country-ipv4.csv.gz"
  printf '\\.\n'
  cat <<'SQL'

-- Un país de DB-IP que no esté en countries.csv rompería la llave foránea más abajo; se avisa cuál es.
do $$
declare faltan text;
begin
  select string_agg(distinct upper(btrim(d.country_code)), ', ') into faltan
  from dbip_csv d
  where upper(btrim(d.country_code)) not in (select upper(btrim(country)) from countries_csv);
  if faltan is not null then
    raise exception 'dbip-country-ipv4.csv usa países que no están en countries.csv: %', faltan;
  end if;
end $$;

truncate ip_to_country, countries;

insert into countries (code, name)
select upper(btrim(country)), btrim(name) from countries_csv;

insert into ip_to_country (ip_from, ip_to, country_code)
select btrim(ip_start)::inet - '0.0.0.0'::inet,
       btrim(ip_end)::inet   - '0.0.0.0'::inet,
       upper(btrim(country_code))
from dbip_csv;

commit;
select (select count(*) from countries) as countries, (select count(*) from ip_to_country) as ip_to_country;
SQL
} | psql "$url" --quiet --no-psqlrc -v ON_ERROR_STOP=1
