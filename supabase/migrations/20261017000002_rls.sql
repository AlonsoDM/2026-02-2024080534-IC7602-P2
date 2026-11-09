-- T-11: RLS y permisos
-- La UI usa Supabase directamente con el rol "authenticated" y solo ve lo de su cuenta
-- El REST API (Worker) usa la service role, que ignora RLS
-- Los hashes nunca se pueden leer: se limitan los permisos por columna.

alter table domains         enable row level security;
alter table urls            enable row level security;
alter table url_file_types  enable row level security;
alter table api_keys        enable row level security;
alter table url_users       enable row level security;
alter table sessions        enable row level security;
alter table zones           enable row level security;
alter table component_keys  enable row level security;
alter table countries       enable row level security;
alter table ip_to_country   enable row level security;

-- ningun permiso para anon ni authenticated
revoke all on domains, urls, url_file_types, api_keys, url_users,
              sessions, zones, component_keys, countries, ip_to_country
  from anon, authenticated;
revoke all on function pais_de_ip(inet) from public, anon, authenticated;

-- domains: crear (solo el nombre; owner_id, token y verified salen de los defaults), ver y eliminar.
-- verified solo lo cambia el Worker, para que nadie se marque un dominio como verificado
grant select on domains to authenticated;
grant insert (name) on domains to authenticated;
grant delete on domains to authenticated;
alter table domains alter column owner_id set default auth.uid();

create policy domains_select on domains for select to authenticated using (owner_id = auth.uid());
create policy domains_insert on domains for insert to authenticated with check (owner_id = auth.uid() and not verified);
create policy domains_delete on domains for delete to authenticated using (owner_id = auth.uid());

-- urls: solo dentro de un dominio propio y verificado
grant select, delete on urls to authenticated;
grant insert (domain_id, pattern, origin, cache_size_bytes, replacement_policy, auth) on urls to authenticated;
grant update (pattern, origin, cache_size_bytes, replacement_policy, auth) on urls to authenticated;

create policy urls_select on urls for select to authenticated
  using (exists (select 1 from domains d where d.id = domain_id and d.owner_id = auth.uid()));
create policy urls_insert on urls for insert to authenticated
  with check (exists (select 1 from domains d where d.id = domain_id and d.owner_id = auth.uid() and d.verified));
create policy urls_update on urls for update to authenticated
  using (exists (select 1 from domains d where d.id = domain_id and d.owner_id = auth.uid()))
  with check (exists (select 1 from domains d where d.id = domain_id and d.owner_id = auth.uid() and d.verified));
create policy urls_delete on urls for delete to authenticated
  using (exists (select 1 from domains d where d.id = domain_id and d.owner_id = auth.uid()));

-- Condición común: la URL pertenece a una cuenta que es la actual.
create function es_mi_url(p_url_id bigint) returns boolean
language sql stable security definer set search_path = public as $$
  select exists (
    select 1 from urls u join domains d on d.id = u.domain_id
    where u.id = p_url_id and d.owner_id = auth.uid()
  );
$$;
revoke all on function es_mi_url(bigint) from public;
grant execute on function es_mi_url(bigint) to authenticated;

-- url_file_types
grant select, insert, update, delete on url_file_types to authenticated;
create policy url_file_types_all on url_file_types for all to authenticated
  using (es_mi_url(url_id)) with check (es_mi_url(url_id));

-- api_keys: se ve todo menos key_hash. La UI genera la llave, la muestra una vez y guarda su hash.
grant select (id, url_id, label, prefix, created_at) on api_keys to authenticated;
grant insert (url_id, label, prefix, key_hash) on api_keys to authenticated;
grant delete on api_keys to authenticated;
create policy api_keys_select on api_keys for select to authenticated using (es_mi_url(url_id));
create policy api_keys_insert on api_keys for insert to authenticated with check (es_mi_url(url_id));
create policy api_keys_delete on api_keys for delete to authenticated using (es_mi_url(url_id));

-- url_users: sin acceso a password_hash. Crear y cambiar contraseña pasan por las funciones de abajo.
grant select (id, url_id, username, created_at) on url_users to authenticated;
grant delete on url_users to authenticated;
create policy url_users_select on url_users for select to authenticated using (es_mi_url(url_id));
create policy url_users_delete on url_users for delete to authenticated using (es_mi_url(url_id));

create function crear_url_user(p_url_id bigint, p_username text, p_password text) returns bigint
language plpgsql security definer set search_path = public, extensions as $$
declare nuevo bigint;
begin
  if not es_mi_url(p_url_id) then
    raise exception 'URL no encontrada' using errcode = '42501';
  end if;
  if length(p_password) < 8 then
    raise exception 'La contraseña debe tener al menos 8 caracteres';
  end if;
  insert into url_users (url_id, username, password_hash)
  values (p_url_id, p_username, crypt(p_password, gen_salt('bf', 10)))
  returning id into nuevo;
  return nuevo;
end $$;

create function cambiar_password_url_user(p_id bigint, p_username text, p_password text) returns void
language plpgsql security definer set search_path = public, extensions as $$
begin
  update url_users u
     set username = coalesce(p_username, u.username),
         password_hash = case when p_password is null then u.password_hash
                              else crypt(p_password, gen_salt('bf', 10)) end
   where u.id = p_id and es_mi_url(u.url_id);
  if not found then
    raise exception 'Usuario no encontrado' using errcode = '42501';
  end if;
  if p_password is not null and length(p_password) < 8 then
    raise exception 'La contraseña debe tener al menos 8 caracteres';
  end if;
end $$;

revoke all on function crear_url_user(bigint, text, text), cambiar_password_url_user(bigint, text, text) from public;
grant execute on function crear_url_user(bigint, text, text), cambiar_password_url_user(bigint, text, text) to authenticated;

-- sessions, zones, component_keys, countries, ip_to_country y pais_de_ip: RLS activo y sin políticas ni permisos
-- para anon/authenticated. Solo la service role del Worker los lee y escribe.
