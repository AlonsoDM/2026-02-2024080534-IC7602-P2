# Supabase: esquema, RLS y datos de prueba

Responsable: PERSONA 2 (T-11). Todo vive en `supabase/` y `just db` lo aplica.

| Archivo | Contenido |
| --- | --- |
| `aplicar.sh` | Lo que corre `just db`: aplica las migraciones pendientes (con el registro de la CLI de Supabase), el seed y IP-to-Country. Solo necesita Docker |
| `migrations/20261017000001_esquema.sql` | Tablas, restricciones, el trigger que valida el patrón de cada URL y `pais_de_ip` |
| `migrations/20261017000002_rls.sql` | RLS, permisos por columna y las funciones de contraseñas |
| `migrations/20261027000001_sesiones.sql` | `urls.tls_enabled` y `verificar_url_user` para el login |
| `seed.sql` | Cuenta de prueba, zonas, llaves de componente y los cuatro orígenes, ya verificados |
| `seed/ip_to_country.sh` | Carga `countries` e `ip_to_country` desde los CSV del P1 (`seed/*.csv*`) |
| `tests/rls_test.sql` | Pruebas de RLS y permisos; `just db-test` |

## Tablas

`domains` (token TXT y estado de verificación) → `urls` (patrón, origen, tamaño, política y tipo de autenticación) → `url_file_types` (extensión y TTL), `api_keys`, `url_users` y `sessions`. Además `zones` (IP y países), `component_keys` (llaves del REST API con alcance `ui`, `interceptor` o `cache`), `countries` e `ip_to_country`.

- Las API Keys y las llaves de componente se guardan como SHA-256; las contraseñas, con bcrypt (`pgcrypto`); las sesiones, como hash del token. Ningún hash sale de Supabase.
- El patrón de una URL debe ser el dominio o un subdominio suyo, con o sin wildcard; lo valida un trigger.
- `verificar_url_user(url_id, username, password)` devuelve el id del usuario si la contraseña coincide con su bcrypt y la URL pide `user_password`, o `NULL`. Así `password_hash` nunca sale de Postgres. Solo la service role la ejecuta.
- `urls.tls_enabled` dice si el host acepta HTTPS del cliente en la caché. Es `false` por defecto; el seed lo enciende para `externo-https.test` y `privado.externo-https.test`. En el proyecto de Supabase la columna ya existía creada a mano, por eso la migración usa `add column if not exists`.
- `pais_de_ip(inet)` busca el rango de mayor `ip_from` que no pase la IP y devuelve `ZZ` si ninguno la contiene o si no es IPv4. Solo la service role la ejecuta.

## Seguridad

- **UI** (rol `authenticated`): ve y modifica solo lo de su cuenta. No puede marcar un dominio como verificado (eso lo hace el Worker), crear URLs en dominios sin verificar, ni leer `key_hash` ni `password_hash`. Los usuarios de una URL se crean con `crear_url_user` y `cambiar_password_url_user`, que guardan el hash.
- **REST API Node.js**: usa la service role, que ignora RLS. `sessions`, `zones`, `component_keys`, `countries` e `ip_to_country` no tienen permisos para `anon` ni `authenticated`.
- **anon**: no ve nada.

## Datos de prueba

`seed.sql` es solo para desarrollo: sus llaves y contraseñas son públicas y están escritas en su encabezado. Las IP de las zonas son de documentación (RFC 5737); se reemplazan con las Elastic IP cuando exista T-24. Los dominios usan `.test` y apuntan a `java-api:8080`, `apache:80` y `httpbin.org` por HTTP y por HTTPS. IP-to-Country da 245 países y 356 333 rangos, igual que en el P1.
