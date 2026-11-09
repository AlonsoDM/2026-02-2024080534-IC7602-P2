-- tls_enabled de cada URL y verificación de contraseñas para /api/auth/login

-- La caché exige tls_enabled en GET /api/cache/config. Por defecto no acepta HTTPS y la UI lo puede encender
alter table urls add column if not exists tls_enabled boolean not null default false;
grant insert (tls_enabled), update (tls_enabled) on urls to authenticated;

-- id del usuario si la contraseña es correcta y la URL pide user_password, NULL si no
-- La comparación se hace aquí con bcrypt para que password_hash nunca salga de Supabase
create function verificar_url_user(p_url_id bigint, p_username text, p_password text) returns bigint
language sql stable security definer set search_path = public as $$
  select uu.id
    from url_users uu
    join urls u on u.id = uu.url_id
   where uu.url_id = p_url_id
     and uu.username = p_username
     and u.auth = 'user_password'
     and uu.password_hash = extensions.crypt(p_password, uu.password_hash);
$$;

-- Para que solo la service role del Worker la llama
revoke all on function verificar_url_user(bigint, text, text) from public, anon, authenticated;
