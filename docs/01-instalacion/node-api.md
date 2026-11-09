# Instalación del REST API Node.js

Responsable: PERSONA 2 (T-12). El REST API es un Worker de Cloudflare en `src/node-api/`. No corre en Kubernetes: se despliega solo en Cloudflare con cada merge a `main`, así que para usarlo no hay que instalar nada. Esta guía es para quien lo desarrolla o tiene que volver a montarlo en otra cuenta.

Producción: `https://zonal-api.duran-munozalonso.workers.dev`

## Requisitos

- Node.js 20 o más reciente, con npm.
- Una cuenta de Cloudflare con acceso a la cuenta del proyecto (Manage Account → Members).
- El proyecto de Supabase de la T-11, con el esquema aplicado (`just db`).

Wrangler, Hono, `supabase-js` y vitest se instalan con el proyecto; no hace falta instalarlos aparte.

## Desarrollo local

Todo se corre desde `src/node-api/`:

```bash
cd src/node-api
npm ci
cp .dev.vars.example .dev.vars
```

En `.dev.vars` van los dos valores de Supabase, que están en Project Settings:

| Variable | Dónde está | Ejemplo |
| --- | --- | --- |
| `SUPABASE_URL` | Data API → Project URL | `https://<código>.supabase.co`, sin `/rest/v1/` |
| `SUPABASE_SECRET_KEY` | API Keys → Secret key | `sb_secret_...` |

`.dev.vars` está en el `.gitignore`. La secret key se salta RLS, así que nunca se sube al repo ni va en la UI.

```bash
npm run dev
```

El Worker queda en `http://localhost:8787`. Con las llaves de `seed.sql` se puede probar todo sin la UI:

```bash
curl -H 'x-api-key: dev-cache-key' http://localhost:8787/api/cache/config
```

Las pruebas no necesitan Supabase. Desde la raíz del repo:

```bash
just api-test
```

## Configuración del Worker

| Nombre | Tipo | Dónde se define | Para qué |
| --- | --- | --- | --- |
| `SUPABASE_URL` | Secret | `wrangler secret put` | URL del proyecto de Supabase |
| `SUPABASE_SECRET_KEY` | Secret | `wrangler secret put` | Acceso a Supabase con la service role |
| `UI_ORIGINS` | Variable | `vars` en `wrangler.jsonc` | Orígenes de la UI que pueden llamar al Worker desde el navegador (CORS), separados por coma |

Los secrets se guardan cifrados en Cloudflare y sobreviven a cada deploy. `UI_ORIGINS` no es secreto: viaja con el código.

## Montarlo en Cloudflare

Esto se hace una sola vez por cuenta. Desde `src/node-api/`:

1. Entrar a Cloudflare: `npx wrangler login`.
2. Primer deploy: `npx wrangler deploy`. Crea el Worker `zonal-api` (el `name` de `wrangler.jsonc`) y da su URL `*.workers.dev`. Si la cuenta no tiene subdominio `workers.dev`, lo pide aquí.
3. Secrets. Cada comando pide el valor en la terminal:

   ```bash
   npx wrangler secret put SUPABASE_URL
   npx wrangler secret put SUPABASE_SECRET_KEY
   ```

   `npx wrangler secret list` muestra los nombres sin los valores.

4. Deploy automático con Workers Builds: Workers & Pages → `zonal-api` → Settings → Build → Connect, con GitHub. La app de Cloudflare la autoriza el dueño del repo.

   | Campo | Valor |
   | --- | --- |
   | Rama de producción | `main` |
   | Root directory | `src/node-api` |
   | Build command | `npm test` |
   | Deploy command | `npx wrangler deploy` |

   Con `npm test` como build, si `tsc` o vitest fallan no se despliega. El nombre del Worker en el dashboard tiene que ser igual al `name` de `wrangler.jsonc`.

## Llaves de componente

Cada componente que llama al API usa su propia llave (ver `docs/03-arquitectura/node-api.md`). En Supabase solo se guarda su SHA-256. Para crear una, se genera la llave y su hash en la terminal:

```bash
LLAVE=$(openssl rand -hex 32)
echo "$LLAVE"
printf '%s' "$LLAVE" | shasum -a 256 | cut -d' ' -f1
```

La llave se le pasa al componente por un canal privado. En el SQL Editor de Supabase solo se pega el hash:

```sql
insert into component_keys (scope, label, key_hash)
values ('cache', 'prod', '<hash>');
```

El alcance es `cache`, `interceptor` o `ui`. Para revocar una llave se borra su fila de `component_keys`; el Worker deja de aceptarla en la siguiente petición.

Las llaves `dev-*` de `seed.sql` son públicas, porque están escritas en el repo. Sirven para desarrollar, pero abren el mismo Supabase que usa producción.

## Verificación

```bash
W=https://zonal-api.duran-munozalonso.workers.dev
```

| Prueba | Comando | Esperado |
| --- | --- | --- |
| El Worker responde | `curl -i $W/health` | `200` |
| Sin llave | `curl -i $W/api/cache/config` | `401` |
| Llave inventada | `curl -i -H 'x-api-key: inventada' $W/api/cache/config` | `401` |
| Llave de otro alcance | `curl -i -H 'x-api-key: <llave ui>' $W/api/cache/config` | `403` |
| Llave de la caché | `curl -i -H 'x-api-key: <llave cache>' $W/api/cache/config` | `200` con las URLs |

Si algo responde `500`, `npx wrangler tail` muestra los logs del Worker en vivo. También quedan en el dashboard, en Workers & Pages → `zonal-api` → Logs.
