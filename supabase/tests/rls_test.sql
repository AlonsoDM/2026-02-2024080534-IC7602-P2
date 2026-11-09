-- T-11: pruebas de RLS y permisos
-- Corre sobre una base con las migraciones y supabase/seed.sql aplicados
--   psql "$SUPABASE_DB_URL" -v ON_ERROR_STOP=1 -f supabase/tests/rls_test.sql
-- Todo ocurre en una transacción que se deshace al final. Si algo falla, psql termina con error

begin;

create schema pruebas;
grant usage on schema pruebas to anon, authenticated, service_role;

create function pruebas.como(p_rol text, p_uid uuid) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claim.sub', coalesce(p_uid::text, ''), true);
  execute format('set local role %I', p_rol);
end $$;

create function pruebas.debe_fallar(p_sql text, p_motivo text) returns void language plpgsql as $$
begin
  begin
    execute p_sql;
  exception when others then
    return;
  end;
  raise exception 'FALLO: debía ser rechazado: %', p_motivo;
end $$;

create function pruebas.esperar(p_real bigint, p_esperado bigint, p_que text) returns void language plpgsql as $$
begin
  if p_real is distinct from p_esperado then
    raise exception 'FALLO: % = %, se esperaba %', p_que, p_real, p_esperado;
  end if;
end $$;


-- Segunda cuenta, sin datos.
insert into auth.users (id, aud, role, email)
values ('00000000-0000-4000-8000-000000000002', 'authenticated', 'authenticated', 'otra@zonal.test');

-- ── Cuenta de prueba: ve lo suyo ────────────────────────────────────────────────────────────────
select pruebas.como('authenticated', '00000000-0000-4000-8000-000000000001');
select pruebas.esperar((select count(*) from domains), 4, 'dominios propios');
select pruebas.esperar((select count(*) from urls), 8, 'URLs propias');
select pruebas.esperar((select count(*) from api_keys), 2, 'API Keys propias');
select pruebas.esperar((select count(*) from url_users), 2, 'usuarios propios');
select pruebas.esperar((select count(*) from url_file_types), 21, 'tipos de archivo propios');

-- Los hashes no se leen.
select pruebas.debe_fallar('select key_hash from api_keys', 'leer key_hash');
select pruebas.debe_fallar('select password_hash from url_users', 'leer password_hash');

-- Tablas del sistema cerradas.
select pruebas.debe_fallar('select * from sessions', 'leer sessions');
select pruebas.debe_fallar('select * from zones', 'leer zones');
select pruebas.debe_fallar('select * from component_keys', 'leer component_keys');
select pruebas.debe_fallar('select * from ip_to_country limit 1', 'leer ip_to_country');
select pruebas.debe_fallar('select pais_de_ip(''8.8.8.8'')', 'ejecutar pais_de_ip');

-- Dominio nuevo: no puede nacer verificado ni con dueño ajeno.
select pruebas.debe_fallar('insert into domains (name, verified, verified_at) values (''trampa.test'', true, now())', 'crear dominio verificado');
select pruebas.debe_fallar('insert into domains (name, owner_id) values (''trampa.test'', ''00000000-0000-4000-8000-000000000002'')', 'crear dominio con otro dueño');
insert into domains (name) values ('nuevo.test');
select pruebas.esperar((select count(*) from domains where name = 'nuevo.test' and not verified and txt_token <> ''), 1, 'dominio nuevo sin verificar con token');
select pruebas.debe_fallar('update domains set verified = true, verified_at = now() where name = ''nuevo.test''', 'verificarse a sí mismo');

-- URLs solo en dominios verificados y con patrón del dominio.
select pruebas.debe_fallar(
  'insert into urls (domain_id, pattern, origin, cache_size_bytes) select id, ''www.nuevo.test'', ''http://x'', 1000 from domains where name = ''nuevo.test''',
  'URL en dominio sin verificar');
select pruebas.debe_fallar(
  'insert into urls (domain_id, pattern, origin, cache_size_bytes) select id, ''www.otro.test'', ''http://x'', 1000 from domains where name = ''apache.test''',
  'patrón fuera del dominio');
select pruebas.debe_fallar(
  'insert into urls (domain_id, pattern, origin, cache_size_bytes) select id, ''www.apache.test'', ''ftp://x'', 1000 from domains where name = ''apache.test''',
  'origen que no es http/https');
insert into urls (domain_id, pattern, origin, cache_size_bytes)
  select id, '*.apache.test', 'http://apache:80', 1000 from domains where name = 'apache.test';
select pruebas.esperar((select count(*) from urls where pattern = '*.apache.test'), 1, 'URL con wildcard creada');

-- Contraseña con hash por función.
select pruebas.debe_fallar(
  'select crear_url_user((select id from urls where pattern = ''privado.apache.test''), ''corta'', ''123'')', 'contraseña corta');
select crear_url_user((select id from urls where pattern = 'privado.apache.test'), 'maria', 'una-clave-larga');
select pruebas.esperar((select count(*) from url_users where username = 'maria'), 1, 'usuario creado');

-- ── La hash de esa contraseña es bcrypt y verifica (se mira como superusuario) ─────────────────
reset role;
select pruebas.esperar((select count(*) from url_users
  where username = 'maria' and password_hash = extensions.crypt('una-clave-larga', password_hash)), 1, 'bcrypt verifica');
select pruebas.esperar((select count(*) from api_keys
  where key_hash = encode(extensions.digest('dev-java-key', 'sha256'), 'hex')), 1, 'sha256 de la API Key de prueba');

-- ── Otra cuenta: no ve ni toca nada ajeno ───────────────────────────────────────────────────────
select pruebas.como('authenticated', '00000000-0000-4000-8000-000000000002');
select pruebas.esperar((select count(*) from domains), 0, 'dominios de otra cuenta');
select pruebas.esperar((select count(*) from urls), 0, 'URLs de otra cuenta');
select pruebas.esperar((select count(*) from api_keys), 0, 'API Keys de otra cuenta');
select pruebas.esperar((select count(*) from url_users), 0, 'usuarios de otra cuenta');
select pruebas.debe_fallar(
  'select crear_url_user((select id from urls limit 1), ''x'', ''12345678'')', 'crear usuario en URL ajena');
select pruebas.debe_fallar(
  'insert into api_keys (url_id, prefix, key_hash) values (1, ''x'', ''y'')', 'crear API Key en URL ajena');
delete from domains;                                              -- no borra nada ajeno
reset role;
select pruebas.esperar((select count(*) from domains), 5, 'dominios tras el borrado ajeno');

-- ── Sin sesión ──────────────────────────────────────────────────────────────────────────────────
select pruebas.como('anon', null);
select pruebas.debe_fallar('select * from domains', 'anon lee dominios');
select pruebas.debe_fallar('select * from urls', 'anon lee URLs');
select pruebas.debe_fallar('select * from api_keys', 'anon lee API Keys');
reset role;

-- ── Service role (el Worker): ve todo ──────────────────────────────────────────────────────────
select pruebas.como('service_role', null);
select pruebas.esperar((select count(*) from domains), 5, 'service_role ve dominios');
select pruebas.esperar((select count(*) from zones), 4, 'service_role ve zonas');
select pruebas.esperar((select count(*) from component_keys), 3, 'service_role ve llaves de componente');
select pruebas.esperar((select (pais_de_ip('8.8.8.8') = 'US')::int), 1, 'pais_de_ip');
reset role;

\echo 'RLS: todas las pruebas pasaron'
rollback;
