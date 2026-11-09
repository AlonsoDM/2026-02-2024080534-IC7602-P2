-- datos de prueba para que nadie dependa de la UI.
-- Cargan una cuenta de prueba y los dominios de los cuatro orígenes (REST API Java, Apache, sitio externo HTTP y sitio externo HTTPS) ya verificados, con sus URLs, tipos de archivo, llaves y usuarios
-- Se puede repetir: no duplica nada, las tablas IP-to-Country se cargan aparte (supabase/seed/ip_to_country.sh)
--
-- Llaves de componente (header x-api-key hacia el REST API Node.js)
--   ui           dev-ui-key
--   interceptor  dev-interceptor-key
--   cache        dev-cache-key
--
-- API Keys de URLs (header x-api-key hacia la caché)
--   seguro.java-api.test   dev-java-key
--   seguro.externo-http.test   dev-http-key
--
-- Usuarios de URLs con contraseña (usuario / contraseña)
--   privado.apache.test        alonso / apache-1234
--   privado.externo-https.test alonso / https-1234
--
-- Cuenta de la UI: pruebas@zonal.test / pruebas-1234





-- Cuenta de prueba en Supabase Auth
insert into auth.users (instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
                        raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
                        confirmation_token, recovery_token, email_change_token_new, email_change)
values ('00000000-0000-0000-0000-000000000000', '00000000-0000-4000-8000-000000000001',
        'authenticated', 'authenticated', 'pruebas@zonal.test',
        extensions.crypt('pruebas-1234', extensions.gen_salt('bf', 10)), now(),
        '{"provider":"email","providers":["email"]}', '{}', now(), now(), '', '', '', '')
on conflict (id) do nothing;

insert into auth.identities (id, user_id, provider_id, provider, identity_data, last_sign_in_at, created_at, updated_at)
values ('00000000-0000-4000-8000-0000000000a1', '00000000-0000-4000-8000-000000000001',
        '00000000-0000-4000-8000-000000000001', 'email',
        '{"sub":"00000000-0000-4000-8000-000000000001","email":"pruebas@zonal.test","email_verified":true}',
        now(), now(), now())
on conflict (id) do nothing;

-- Llaves de componente
insert into component_keys (scope, label, key_hash)
values ('ui',          'dev', encode(extensions.digest('dev-ui-key', 'sha256'), 'hex')),
       ('interceptor', 'dev', encode(extensions.digest('dev-interceptor-key', 'sha256'), 'hex')),
       ('cache',       'dev', encode(extensions.digest('dev-cache-key', 'sha256'), 'hex'))
on conflict (key_hash) do nothing;

-- Zonas
-- Las IP son de documentación (RFC 5737); se reemplazan por las Elastic IP reales cuando exista T-24
insert into zones (code, ip, countries)
values ('CR', '203.0.113.11', '{CR,PA,NI,HN,GT,SV,BZ}'),
       ('US', '203.0.113.12', '{US,CA,MX}'),
       ('ES', '203.0.113.13', '{ES,PT,FR,IT,DE}'),
       ('GB', '203.0.113.24', '{GB,IE}')
on conflict (code) do nothing;

  -- Dominios ya verificados (los sitios externos no permiten crear su TXT).
insert into domains (owner_id, name, txt_token, verified, verified_at)
values ('00000000-0000-4000-8000-000000000001', 'java-api.test',      'dev-token-java-api',      true, now()),
       ('00000000-0000-4000-8000-000000000001', 'apache.test',        'dev-token-apache',        true, now()),
       ('00000000-0000-4000-8000-000000000001', 'externo-http.test',  'dev-token-externo-http',  true, now()),
       ('00000000-0000-4000-8000-000000000001', 'externo-https.test', 'dev-token-externo-https', true, now())
on conflict (name) do nothing;

-- URLs
-- Los origenes usan los nombres de servicio de Kubernetes, los sitios externos son httpbin.org.
insert into urls (domain_id, pattern, origin, cache_size_bytes, replacement_policy, auth)
select d.id, v.pattern, v.origin, v.size, v.policy::replacement_policy, v.auth::auth_type
from (values
  ('java-api.test',      'java-api.test',              'http://java-api:8080',  1048576, 'lru',    'none'),
  ('java-api.test',      'seguro.java-api.test',       'http://java-api:8080',  1048576, 'lfu',    'api_key'),
  ('apache.test',        'apache.test',                'http://apache:80',      5242880, 'lru',    'none'),
  ('apache.test',        'privado.apache.test',        'http://apache:80',      5242880, 'fifo',   'user_password'),
  ('externo-http.test',  'externo-http.test',          'http://httpbin.org',    2097152, 'mru',    'none'),
  ('externo-http.test',  'seguro.externo-http.test',   'http://httpbin.org',    2097152, 'random', 'api_key'),
  ('externo-https.test', 'externo-https.test',         'https://httpbin.org',   2097152, 'lru',    'none'),
  ('externo-https.test', 'privado.externo-https.test', 'https://httpbin.org',   2097152, 'lfu',    'user_password')
) as v(dominio, pattern, origin, size, policy, auth)
join domains d on d.name = v.dominio
on conflict (pattern) do nothing;

-- Los sitios externos por HTTPS también aceptan HTTPS del cliente, igual que en el proyecto de Supabase
update urls set tls_enabled = true where pattern in ('externo-https.test', 'privado.externo-https.test');

-- Tipos de archivo y TTL: JSON corto para las APIs, estaticos mas largos para Apache e imagenes
insert into url_file_types (url_id, extension, ttl_seconds)
select u.id, t.extension, t.ttl
from urls u
join (values
  ('java-api.test', 'json', 30), ('seguro.java-api.test', 'json', 30),
  ('apache.test', 'html', 60), ('apache.test', 'css', 300), ('apache.test', 'js', 300),
  ('apache.test', 'png', 600), ('apache.test', 'jpg', 600),
  ('privado.apache.test', 'html', 60), ('privado.apache.test', 'css', 300),
  ('privado.apache.test', 'js', 300), ('privado.apache.test', 'png', 600),
  ('externo-http.test', 'json', 30), ('externo-http.test', 'html', 60), ('externo-http.test', 'png', 600),
  ('seguro.externo-http.test', 'json', 30), ('seguro.externo-http.test', 'html', 60),
  ('externo-https.test', 'json', 30), ('externo-https.test', 'html', 60), ('externo-https.test', 'png', 600),
  ('privado.externo-https.test', 'json', 30), ('privado.externo-https.test', 'html', 60)
) as t(pattern, extension, ttl) on t.pattern = u.pattern
on conflict (url_id, extension) do nothing;

-- API Keys de las URLs con auth = api_key.
insert into api_keys (url_id, label, prefix, key_hash)
select u.id, 'dev', left(k.llave, 8), encode(extensions.digest(k.llave, 'sha256'), 'hex')
from urls u
join (values ('seguro.java-api.test', 'dev-java-key'), ('seguro.externo-http.test', 'dev-http-key'))
  as k(pattern, llave) on k.pattern = u.pattern
on conflict (key_hash) do nothing;

-- Usuarios de las URLs con auth = user_password.
insert into url_users (url_id, username, password_hash)
select u.id, x.username, extensions.crypt(x.password, extensions.gen_salt('bf', 10))
from urls u
join (values ('privado.apache.test', 'alonso', 'apache-1234'), ('privado.externo-https.test', 'alonso', 'https-1234'))
  as x(pattern, username, password) on x.pattern = u.pattern
on conflict (url_id, username) do nothing;
