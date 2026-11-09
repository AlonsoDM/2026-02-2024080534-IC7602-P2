# REST API Node.js: autenticación y autorización

Responsable: PERSONA 2 (T-12). Todo vive en `src/node-api/`, un Worker de Cloudflare escrito con Hono y `supabase-js`.

| Archivo | Contenido |
| --- | --- |
| `src/index.ts` | `createApp`: `/health`, CORS, el cliente de Supabase y el montaje de cada grupo de rutas |
| `src/supabase.ts` | `createSupabase`: el cliente con la secret key |
| `src/middleware/require-scope.ts` | `requireScope` y `sha256Hex` |
| `src/middleware/cors.ts` | CORS para la UI |
| `src/routes/*.ts` | Un archivo por grupo: `cache`, `interceptor`, `auth` y `domains` |
| `test/` | Pruebas con vitest y un Supabase falso en memoria; `just api-test` |
| `wrangler.jsonc` | Nombre del Worker, `nodejs_compat`, `UI_ORIGINS` y `SESSION_TTL_SECONDS` |

## Quién llama al API

Ninguno de los que llama al API es una persona con cuenta: son componentes. Por eso no se usan JWT ni sesiones de Supabase Auth, sino una **llave de componente** por cada uno, en el header `x-api-key`. Cada llave tiene un **alcance** (`component_scope` en Supabase) y solo abre las rutas de su grupo.

| Alcance | La usa | Abre |
| --- | --- | --- |
| `cache` | Zonal Cache | `/api/cache/*` |
| `interceptor` | DNS Interceptor | `/api/interceptor/*` |
| `ui` | UI en el navegador | `/api/auth/*` y `/api/domains/*` |

`/health` no pide llave: solo dice que el Worker responde.

No hay que confundirlas con las API Keys que cada usuario crea en la UI para proteger sus URLs (`api_keys`). Esas las manda el cliente final a la caché, y la caché pregunta por ellas con `POST /api/cache/api-keys/verify` (T-13).

## Cómo se valida una llave

`requireScope(alcance)` corre antes de cada ruta de su grupo:

1. Lee `x-api-key`. Si no viene, responde `401`.
2. Calcula su SHA-256 en hexadecimal con `crypto.subtle.digest`. Da lo mismo que `encode(digest(..., 'sha256'), 'hex')` en Postgres, que es como `seed.sql` guarda los hashes.
3. Busca ese hash en `component_keys`. Si no está, responde `401`.
4. Si la llave es de otro alcance, responde `403`.
5. Si todo está bien, deja el alcance en `c.var.scope` y sigue a la ruta.

Si Supabase no responde, se da `500` y no `401`: así una caída no se confunde con una llave mala.

| Código | Cuándo |
| --- | --- |
| `401` | Falta `x-api-key` o la llave no existe: no sabemos quién es |
| `403` | La llave existe pero es de otro componente: sabemos quién es, y no tiene permiso |
| `500` | Supabase falló al validar la llave o al leer los datos |

Se guarda solo el hash para que una fuga de la base no deje usar las llaves. La búsqueda se hace por hash exacto en una columna `unique`, así que la comparación ocurre dentro de Postgres y no hace falta una comparación de tiempo constante en el Worker.

## La llave de la UI es pública

La UI corre en el navegador, así que su llave viaja en el bundle de JavaScript y cualquiera la puede leer. No hay forma de esconderla. Por eso su alcance es el mínimo: solo abre el login, el logout y la verificación TXT. Ninguna de esas rutas entrega datos sin pedir algo más: el login exige el usuario y la contraseña de la URL, y la verificación TXT solo marca un dominio si el registro DNS tiene su token.

Las llaves de las cachés y del Interceptor sí son secretas: corren en el servidor y nunca llegan a un navegador. Se les pasan por variable de entorno y no se suben al repo.

## La secret key de Supabase

El Worker entra a Supabase con la secret key, que se salta RLS. Vive solo en el Worker: en `.dev.vars` en local, que git ignora, y como secret de Cloudflare en producción (`wrangler secret put`). Nunca va en la UI.

## CORS

La UI llama al Worker desde otro origen, así que el navegador primero manda un preflight `OPTIONS`. Ese preflight no trae `x-api-key`, por eso CORS va antes de `requireScope` y lo responde sin pedir llave. Solo se autorizan los orígenes de `UI_ORIGINS` en `wrangler.jsonc`, separados por coma. Son `http://localhost:5173` para Vite en local y `https://zonal-ui.pages.dev`, la UI en Cloudflare Pages. Las URLs de preview de Pages (`HASH.zonal-ui.pages.dev`) no están autorizadas a propósito.

CORS no es seguridad para el API: un cliente fuera del navegador lo ignora. Lo que protege cada ruta es la llave.

## Cómo agregar una ruta

Cada grupo ya tiene su archivo en `src/routes/` con su `requireScope`, así que no hay que tocar la seguridad. `interceptor.get('/resolve', ...)` queda en `/api/interceptor/resolve` y solo entra la llave `interceptor`. Las pruebas usan `createApp(fakeSupabase({...}))`, como en `test/cache-config.test.ts` y `test/interceptor.test.ts`.

## `GET /api/interceptor/resolve`

Recibe `domain` y `client_ip`. El dominio se normaliza a minúsculas y se permite un punto final; se validan las etiquetas DNS y se reutiliza la utilidad IPv4 de Hono. Solo se consideran patrones de `urls` cuyo dominio esté verificado. Un patrón exacto no incluye subdominios; `*.marca.com` incluye `marca.com` y sus subdominios de cualquier profundidad, sin coincidir con `otramarca.com`.

Si no hay coincidencia, devuelve HTTP `200` con `{ "exists": false }` sin consultar geolocalización. Si existe, llama al RPC ya disponible `pais_de_ip(p_ip)` y busca la primera fila de `zones` cuyo arreglo `countries` contenga el país. Devuelve `{ "exists": true, "zone": "GB", "address": "203.0.113.24" }`.

No hay países ni asignaciones de zonas escritos en el código. Quitar un país de todas sus zonas produce un error en la siguiente consulta. Si varias zonas lo incluyen, se usa la primera devuelta por la BD, sin imponer un orden. Un país desconocido (`ZZ`), sin zona o con una caché sin IPv4 válida produce HTTP `500`, al igual que un fallo de Supabase; nunca se devuelve `exists: false` para esos errores. Parámetros ausentes o inválidos producen HTTP `400`. El cliente C transforma cualquier HTTP distinto de `200` en DNS `SERVFAIL`.

Las pruebas unitarias cubren coincidencias, verificación del dominio, parámetros inválidos, tres países, zonas repetidas, eliminación de la asignación y fallos de BD. Para probar contra un API en ejecución con los datos del seed:

```bash
NODE_API_URL=http://127.0.0.1:8787 NODE_API_KEY=dev-interceptor-key \
  node src/node-api/test/interceptor-live.mjs
```

La prueba usa `apache.test` (se puede cambiar con `INTERCEPTOR_TEST_DOMAIN`) y comprueba US (`8.8.8.8`), ES (`80.58.61.250`) y GB (`81.2.69.160`), además de un nombre ausente y una IP inválida. Las IPs se contrastaron con el CSV IP-to-Country del proyecto. Se ejecutó contra el manejador Hono con el cliente real de Supabase, PostgREST y PostgreSQL temporales: las migraciones y el seed cargaron 245 países y 356 333 rangos, y las tres resoluciones pasaron. Quitar US de `zones.countries` produjo HTTP `500` sin cambiar código. La comunicación del Interceptor C con ese API local también pasó: sin país conocido respondió `SERVFAIL`; con un rango de prueba para localhost devolvió la IP de GB con TTL 60. Ese rango solo se agregó en la base temporal y se retiró al terminar. Pasaron 73 pruebas del API, 22 del Interceptor y 59 de la caché; la compilación de Wrangler con `--dry-run` también pasó, sin publicar. Falta repetir la validación contra el Worker desplegado con sus credenciales.

## `GET /api/cache/config`

Devuelve las URLs de los dominios **verificados**, ordenadas por `id`, con sus tipos de archivo ordenados por extensión y con el formato de la sección 5 del README, incluido `tls_enabled`. Usa un `inner join` con `domains` para filtrar los que no están verificados. No devuelve ningún hash: las API Keys y contraseñas se validan en sus propias rutas.

La caché descarta cada URL que no traiga `tls_enabled` como booleano, así que sin ese campo carga 0 URLs y responde `404` a todo. La columna queda en la migración `20261027000001_sesiones.sql`, con `false` por defecto.

## Sesiones de usuario y contraseña (T-14)

Las tres rutas trabajan sobre la tabla `sessions`. El token de sesión son 32 bytes aleatorios de `crypto.getRandomValues` en hexadecimal (64 caracteres). En Supabase solo queda su SHA-256, igual que las API Keys, así que una fuga de la base no deja usar las sesiones. Cada sesión es de una sola URL.

### `POST /api/auth/login` (llave `ui`)

```
{ "url_id": 4, "username": "alonso", "password": "apache-1234" }   →   { "session": "<64 hex>", "expires_in": 3600 }
```

La contraseña se compara con el RPC `verificar_url_user`, que corre `crypt` (bcrypt) dentro de Postgres; el Worker nunca ve `password_hash`. El RPC solo acepta URLs con `auth = user_password`. Si las credenciales no sirven responde `401` con el mismo mensaje para un usuario inexistente y una contraseña equivocada, para no revelar cuál usuario existe. La sesión expira a los `SESSION_TTL_SECONDS` (en `wrangler.jsonc`, `3600` por defecto; si falta o no es un entero positivo se usa una hora).

### `POST /api/auth/logout` (llave `ui`)

```
{ "session": "<64 hex>" }   →   { "ok": true }
```

Borra la sesión. Responde `ok` aunque ya no existiera. La cookie se queda en el navegador, pero la caché la rechaza en la siguiente petición y vuelve al formulario.

### `POST /api/cache/sessions/verify` (llave `cache`)

```
{ "url_id": 4, "session": "<64 hex>" }   →   { "valid": true }
```

Busca el hash con ese `url_id` y `expires_at` en el futuro, igual que `api-keys/verify`.

En las tres, un cuerpo que no sirve da `400` y un fallo de Supabase da `500`. Las pruebas están en `test/sesiones.test.ts`.

## Verificación de dominios por TXT (T-30)

### `POST /api/domains/:id/verify` (llave `ui`)

Comprueba que quien registró el dominio controla su DNS. Al agregar un dominio, Supabase le asigna un `txt_token` aleatorio (`domains.txt_token`). La persona crea un registro TXT con ese valor, y la UI llama a esta ruta.

**Dónde va el token.** En un registro TXT del dominio mismo, no de un subdominio. Para `marca.com`:

```
marca.com.   TXT   "<txt_token>"
```

**Cómo se verifica.** El Worker lee el dominio de `domains` por `id` y consulta sus TXT por DNS sobre HTTPS al resolver de Cloudflare:

```
GET https://cloudflare-dns.com/dns-query?name=marca.com&type=TXT
accept: application/dns-json
```

Se usa DoH porque un Worker de Cloudflare no puede abrir sockets UDP para hablar DNS directamente. Cada TXT llega entre comillas y un valor largo puede venir en varios trozos (`"abc" "def"`); se quitan las comillas y los trozos se juntan. El dominio se marca como verificado solo si algún TXT es **exactamente** igual al token. El token sale de Supabase, nunca de la petición, así que quien llama no puede decidir contra qué se compara. Al verificar se guardan `verified = true` y `verified_at` juntos, como exige el `check` de la tabla.

| Código | Cuándo |
| --- | --- |
| `200` con `{ "verified": true }` | El TXT trae el token, o el dominio ya estaba verificado (en ese caso no se consulta el DNS) |
| `200` con `{ "verified": false }` | El DNS respondió pero ningún TXT trae el token, o el dominio no tiene TXT |
| `400` | El `id` no es un entero positivo |
| `401` / `403` | Falta la llave `x-api-key` o no es la de la UI |
| `404` | No existe un dominio con ese `id` |
| `500` | Supabase falló al leer el dominio o al guardar la verificación |
| `502` | El resolver DoH falló o respondió un error distinto de "el nombre no existe" |

Un TXT que no coincide no es un error del servidor, por eso responde `200` y la UI muestra "todavía no se encuentra el registro". Los dominios de los sitios externos del seed ya están verificados, porque no se puede crear su TXT. La ruta no hace falta para ellos.

Las pruebas están en `test/domains.test.ts` y simulan Supabase y el resolver DoH, sin red.
