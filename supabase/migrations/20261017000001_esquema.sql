-- Esquema base del proyecto
-- Las cuentas viven en `auth.users` todo lo demás cuelga de ellas
-- Las llaves y contraseñas se guardan siempre con hash nunca salen de supabase

create extension if not exists pgcrypto with schema extensions;

create type replacement_policy as enum ('lru', 'lfu', 'fifo', 'mru', 'random');
create type auth_type as enum ('none', 'api_key', 'user_password');
create type component_scope as enum ('ui', 'interceptor', 'cache');

-- Dominios registrados por una cuenta se verifican con un registro TXT que contiene txt_token
create table domains (
  id          bigint generated always as identity primary key,
  owner_id    uuid not null references auth.users (id) on delete cascade,
  name        text not null unique check (name = lower(name) and name ~ '^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$'),
  txt_token   text not null unique default encode(extensions.gen_random_bytes(16), 'hex'),
  verified    boolean not null default false,
  verified_at timestamptz,
  created_at  timestamptz not null default now(),
  check (verified = (verified_at is not null))
);
create index domains_owner_idx on domains (owner_id);

-- URLs (hosts) de un dominio, el patrón puede ser exacto o llevar wildcard: "api.marca.com" o "*.marca.com".
create table urls (
  id                 bigint generated always as identity primary key,
  domain_id          bigint not null references domains (id) on delete cascade,
  pattern            text not null unique check (pattern = lower(pattern)),
  origin             text not null check (origin ~ '^https?://'),
  cache_size_bytes   bigint not null check (cache_size_bytes > 0),
  replacement_policy replacement_policy not null default 'lru',
  auth               auth_type not null default 'none',
  created_at         timestamptz not null default now()
);
create index urls_domain_idx on urls (domain_id);

-- El patrón debe pertenecer al dominio: igual a él, o un subdominio (con o sin wildcard) de el
create function urls_validar_patron() returns trigger
language plpgsql as $$
declare
  dominio text;
  base    text := regexp_replace(new.pattern, '^\*\.', '');
begin
  select name into dominio from domains where id = new.domain_id;
  if base <> dominio and base not like '%.' || dominio then
    raise exception 'El patrón % no pertenece al dominio %', new.pattern, dominio;
  end if;
  return new;
end $$;

create trigger urls_validar_patron
before insert or update of pattern, domain_id on urls
for each row execute function urls_validar_patron();

-- Tipos de archivo cacheables de una URL y su TTL.
create table url_file_types (
  url_id      bigint not null references urls (id) on delete cascade,
  extension   text not null check (extension = lower(extension) and extension ~ '^[a-z0-9]+$'),
  ttl_seconds integer not null check (ttl_seconds > 0),
  primary key (url_id, extension)
);

-- API Keys (header x-api-key) de una URL con auth = api_key. Se guarda el SHA-256 de la llave.
create table api_keys (
  id         bigint generated always as identity primary key,
  url_id     bigint not null references urls (id) on delete cascade,
  label      text not null default '',
  prefix     text not null,                 -- primeros caracteres, para reconocerla en la UI
  key_hash   text not null unique,          -- sha256 hex de la llave completa
  created_at timestamptz not null default now()
);
create index api_keys_url_idx on api_keys (url_id);

-- Usuarios y contraseñas de una URL con auth = user_password. Contraseña con bcrypt.
create table url_users (
  id            bigint generated always as identity primary key,
  url_id        bigint not null references urls (id) on delete cascade,
  username      text not null check (length(username) > 0),
  password_hash text not null,
  created_at    timestamptz not null default now(),
  unique (url_id, username)
);

-- Sesiones creadas por /api/auth/login. Se guarda el hash del token de sesión.
create table sessions (
  id           bigint generated always as identity primary key,
  url_id       bigint not null references urls (id) on delete cascade,
  url_user_id  bigint not null references url_users (id) on delete cascade,
  token_hash   text not null unique,
  expires_at   timestamptz not null,
  created_at   timestamptz not null default now()
);
create index sessions_expires_idx on sessions (expires_at);

-- Zonas de caché: la IP pública de la caché y los países que atiende.
create table zones (
  code       text primary key check (code = upper(code)),
  ip         inet not null,
  countries  text[] not null default '{}'
);

-- Llaves de componente del REST API (UI, Interceptor, cachés), cada una con su alcance.
create table component_keys (
  id         bigint generated always as identity primary key,
  scope      component_scope not null,
  label      text not null default '',
  key_hash   text not null unique,
  created_at timestamptz not null default now()
);

-- IP-to-Country del P1. countries.csv: los países por código ISO (ZZ = desconocido)
create table countries (
  code char(2) primary key check (code = upper(code)),
  name text not null
);

-- dbip-country-ipv4.csv: rangos IPv4 como enteros de 32 bits, sin traslapes por construcción de DB-IP
-- La búsqueda de una IP usa la llave primaria: el rango de mayor ip_from que no la supere
create table ip_to_country (
  ip_from      bigint primary key check (ip_from between 0 and 4294967295),
  ip_to        bigint not null check (ip_to between 0 and 4294967295),
  country_code char(2) not null references countries (code),
  check (ip_from <= ip_to)
);

-- Pais de una IPv4. Devuelve 'ZZ' si ningún rango la contiene o si no es IPv4.
create function pais_de_ip(p_ip inet) returns char(2)
language sql stable set search_path = public as $$
  with ip as (select case when family(p_ip) = 4 then p_ip - '0.0.0.0'::inet end as n)
  select coalesce(
    (select c.country_code
       from ip,
            lateral (select r.ip_to, r.country_code
                       from ip_to_country r
                      where r.ip_from <= ip.n
                      order by r.ip_from desc
                      limit 1) c
      where c.ip_to >= ip.n),
    'ZZ');
$$;
