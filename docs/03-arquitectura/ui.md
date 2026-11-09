# UI: cuentas, dominios, URLs y credenciales

La UI está en `src/ui/`. Es una aplicación de una sola página con React 19, Vite, TypeScript y Tailwind CSS 4, que se despliega en Cloudflare Pages (proyecto `zonal-ui`, `https://zonal-ui.pages.dev`).

## Quién habla con quién

```
Navegador ──► Supabase Auth     registro, inicio y cierre de sesión
          ├─► Supabase (RLS)    ver, agregar y eliminar dominios, con la sesión de la persona
          └─► REST API Node.js  solo POST /api/domains/:id/verify, con la llave de la UI
```

La UI usa Supabase directamente para su CRUD, igual que en el P1. Lo que protege los datos es RLS, no la UI: aunque alguien edite el JavaScript, solo ve y borra lo suyo. Lo único que pasa por el Worker es la verificación TXT, porque necesita privilegios para marcar un dominio como verificado (ver `node-api.md`).

## Qué hay en el navegador

Todo lo que va en las variables `VITE_*` termina en el JavaScript que baja el navegador, así que cualquiera lo puede leer:

| Variable | Qué es | Por qué es seguro que sea pública |
| --- | --- | --- |
| `VITE_SUPABASE_URL` | URL del proyecto de Supabase | Es una dirección, no un secreto |
| `VITE_SUPABASE_PUBLISHABLE_KEY` | Llave `publishable` de Supabase | Es pública por diseño; lo que limita el acceso es RLS |
| `VITE_API_URL` | URL del Worker | Es una dirección |
| `VITE_API_KEY` | Llave de componente con alcance `ui` | Su alcance es el mínimo: solo login, logout y verificación TXT (ver `node-api.md`) |

Nunca se pone en la UI la `secret key` ni la `service_role` de Supabase. En local se llenan en `src/ui/.env.local`, que git ignora, a partir de `src/ui/.env.example`. En Cloudflare Pages se definen en las variables de entorno del proyecto, porque Vite las incrusta al compilar.

## Pantallas

| Ruta | Archivo | Qué hace |
| --- | --- | --- |
| `/entrar` | `src/pages/Entrar.tsx` | Crear cuenta e iniciar sesión con correo y contraseña. Si Supabase pide confirmar el correo, avisa que hay que abrir el enlace |
| `/` | `src/pages/Dominios.tsx` | Lista los dominios de la cuenta. Permite agregar uno, ver el registro TXT que hay que crear (tipo, nombre y valor), verificarlo y eliminarlo. Cerrar sesión está arriba a la derecha |
| `/dominios/:id` | `src/pages/Urls.tsx` | URLs de un dominio verificado: crear, modificar y eliminar, con las credenciales de cada una (T-32) |
| `/login` | `src/pages/Login.tsx` | Formulario de usuario y contraseña de una URL protegida. Es pública y la usa la caché, no quien administra la cuenta (T-33) |

`src/components/Ruta.tsx` manda a `/entrar` a quien no tiene sesión. `AuthProvider` mantiene la sesión de Supabase al día para toda la aplicación.

## Dominios

- **Agregar.** La UI solo manda el `name` en minúsculas. `owner_id`, `txt_token` y `verified` salen de los defaults de la tabla, y RLS solo permite insertar esa columna, así que nadie puede crearse un dominio ya verificado. Un nombre repetido (`23505`) se muestra como "ya está registrado".
- **Verificar.** Llama a `POST /api/domains/:id/verify` con la llave de la UI. Si responde `verified: true`, la lista se recarga. Si responde `false`, la UI avisa que el registro TXT todavía no se encuentra, porque un TXT recién creado puede tardar unos minutos en propagarse. Un `502` del Worker se muestra como un error del DNS para que se intente de nuevo.
- **Eliminar.** Pide confirmación y borra el dominio; sus URLs, API Keys y usuarios se eliminan en cascada.

## URLs (T-32)

Desde un dominio **verificado**, el botón "Administrar URLs" lleva a `/dominios/:id`. Un dominio sin verificar muestra un aviso en lugar del formulario; además, RLS rechaza el insert en `urls` si el dominio no está verificado.

El formulario (`src/components/UrlForm.tsx`) sirve para crear y para modificar, y pide:

| Campo | Regla |
| --- | --- |
| Patrón | El dominio o un subdominio suyo, exacto o con `*.` al inicio (`marca.com`, `api.marca.com`, `*.marca.com`). La UI lo revisa antes de enviar, con la misma regla del trigger `urls_validar_patron` de Supabase, para dar un mensaje claro |
| Origen | URL con `http://` o `https://` |
| Tamaño máximo | En MB, mayor que cero; se guarda en bytes (`cache_size_bytes`) |
| Política de reemplazo | LRU, LFU, FIFO, MRU o aleatoria |
| Autenticación | Ninguna, API Key o usuario y contraseña |
| Tipos de archivo | Al menos uno, con su extensión (sin punto, en minúsculas) y su TTL. El TTL se escribe en segundos, minutos, horas o días y se guarda en segundos |

Los tipos de archivo se reemplazan completos al guardar: se borran los de la URL y se insertan los de la pantalla. Si falla ese paso al crear una URL, la URL se borra para no dejar una sin tipos, porque la caché solo guarda los tipos listados. Un patrón repetido (`23505`) se muestra como "ya existe una URL con ese patrón". Si se cambia el tipo de autenticación, las credenciales anteriores no se borran, pero la caché solo usa las del tipo elegido.

## Credenciales (T-32)

Según el tipo de autenticación de la URL, cada tarjeta muestra uno de dos paneles.

**API Keys** (`src/components/ApiKeys.tsx`). La llave se genera en el navegador con `crypto.getRandomValues`: `zc_` más 64 caracteres hexadecimales (256 bits). A Supabase solo se envía su hash SHA-256 (`key_hash`), calculado igual que `sha256Hex` del Worker, y sus primeros 8 caracteres (`prefix`), para reconocerla en la lista. La llave completa se muestra **una sola vez**, con un botón para copiarla, y no se puede volver a leer: RLS no deja leer `key_hash` y la UI no la guarda. Crear y eliminar llaves son las únicas operaciones, porque una llave no se modifica.

**Usuarios y contraseñas** (`src/components/UsuariosUrl.tsx`). La UI nunca lee ni manda un hash. Crear usa la función `crear_url_user` y modificar usa `cambiar_password_url_user`: las dos guardan la contraseña con bcrypt dentro de Supabase y exigen que la URL sea de la cuenta. La contraseña debe tener al menos 8 caracteres. Al modificar, el nombre de usuario se puede cambiar, y dejar la contraseña vacía la conserva. Eliminar un usuario cierra sus sesiones abiertas, por el `on delete cascade` de `sessions`.

## Formulario de login de las URLs (T-33)

Cuando una URL tiene `auth = user_password` y la petición no trae una sesión válida, la caché responde `302` a `UI_LOGIN_URL` (`https://zonal-ui.pages.dev/login`) con dos parámetros (ver `zonal-cache.md`):

```
/login?url=<id de la URL>&callback=<esquema>://<host>/__auth/callback?next=<ruta original>
```

`Login.tsx` hace esto:

1. Lee `url` y `callback`. Si faltan o no son válidos, muestra "Enlace no válido" y **no** muestra el formulario.
2. Pide usuario y contraseña, y muestra el host al que se va a entrar, para que se vea a dónde irá la sesión.
3. Llama a `POST /api/auth/login` con `{ url_id, username, password }` y la llave de la UI. Con `401` muestra "Usuario o contraseña incorrectos" y borra la contraseña del campo; con un fallo de red o un `5xx`, un mensaje distinto, para no confundir un error del servidor con una contraseña mala.
4. Con la sesión que responde el Worker, redirige a `callback&session=<sesión>` con `location.replace`, para que "atrás" no regrese a un formulario que ya cumplió. La caché valida la sesión con `POST /api/cache/sessions/verify`, la deja en la cookie `zc_session` de su propio dominio y vuelve a la ruta original.

La sesión no se guarda en la UI: ni en `localStorage` ni en cookies. Solo pasa por la dirección del callback.

### El callback viene de un enlace

Cualquiera puede escribir un enlace a `/login` con el `callback` que quiera. Si la UI redirigiera sin revisar, un enlace armado por otra persona mandaría la sesión de quien inicia sesión a un sitio ajeno. Por eso `leerParametros` (`src/lib/callback.ts`) acepta el callback solo si cumple lo que arma la caché:

- `url` es un entero positivo.
- El callback es una dirección `http:` o `https:`, sin usuario ni contraseña (`https://sitio.com@otro.com` engaña a quien lo lee).
- Su ruta es exactamente `/__auth/callback`, nunca otra página.
- No trae ya un `session` ni un fragmento (`#`) que lo esconda.

**Límite conocido.** La UI no comprueba que el host del callback pertenezca a la URL `url`. Hacerlo exige un dato que el navegador no puede leer (un visitante no tiene acceso a `urls` por RLS) y una ruta nueva en el Worker. Lo que limita el daño es que la sesión solo vale para esa `url_id` y expira según `SESSION_TTL_SECONDS`, y que el formulario muestra el host de destino. Si se quisiera cerrar del todo, la ruta `POST /api/auth/login` podría devolver la sesión solo después de comparar el host con el `pattern` de la URL.

### Cómo se probó

Con la caché real (imagen construida de `src/zonal-cache`) cargando su configuración desde el Worker desplegado, y la UI con `npm run dev`, en Chrome y contra `privado.externo-https.test` (URL `8`, usuario `alonso`, del seed): sin sesión, la caché manda al formulario con `url=8` y el `callback` con la ruta original; una contraseña mala muestra el error y se queda; con la buena vuelve a la caché, se ve el contenido del origen, la cookie `zc_session` queda `HttpOnly` y `SameSite=Lax`, la sesión no queda en la barra de direcciones y la siguiente petición entra directo. Los enlaces con callback a otro sitio, con credenciales o con `javascript:` muestran "Enlace no válido" y la página no contacta ningún otro host. `src/lib/callback.test.ts` cubre las reglas de validación con 23 pruebas.

## Pruebas

`npm test` en `src/ui` corre 56 pruebas de vitest sobre la lógica que no depende del navegador: validación de patrón, origen y tipos de archivo, conversión de MB y de TTL, el SHA-256 contra el valor del estándar, el formato de las llaves generadas y la validación del callback del login.

Además se probó en Chrome, con una cuenta real contra Supabase y el Worker desplegado, este recorrido en un dominio verificado: rechazo de un patrón de otro dominio y de un origen inválido; crear una URL con API Key de 2 MB y LFU; crear una API Key y comprobar que `POST /api/cache/api-keys/verify` la acepta, rechaza una alterada y deja de aceptarla al eliminarla; modificar la URL a 3 MB, FIFO y usuario y contraseña; crear un usuario e iniciar sesión con `POST /api/auth/login`; rechazo de contraseña corta y de usuario repetido; cambiar el nombre y la contraseña (la vieja da `401`, la nueva `200`); eliminar el usuario y la URL.

## Despliegue

Cloudflare Pages construye con cada merge a `main`:

| Campo | Valor |
| --- | --- |
| Production branch | `main` |
| Root directory | `src/ui` |
| Build command | `npm run build` |
| Build output directory | `dist` |
| Variables de entorno | las cuatro `VITE_*` de la tabla, y `NODE_VERSION=22` |

`public/_redirects` contiene `/*  /index.html  200`. Sin esa regla, recargar una ruta como `/entrar` daría `404`, porque en el servidor solo existe `index.html`.

El Worker solo acepta las llamadas del navegador desde los orígenes de `UI_ORIGINS` (ver la sección CORS de `node-api.md`). Si se cambia el nombre del proyecto de Pages, hay que actualizar esa variable en `wrangler.jsonc`.

En Supabase, en Authentication, URL Configuration, el Site URL debe ser la URL de Pages. Si no, los enlaces de confirmación de correo apuntan a `localhost`.

## Cómo correrla en local

```
cd src/ui
cp .env.example .env.local     # llenar las cuatro variables
npm install
npm run dev                    # http://localhost:5173
npm run build                  # compila y revisa los tipos
npm test                       # pruebas de vitest
npm run lint
```

El puerto `5173` es fijo (`strictPort`) porque el Worker solo acepta ese origen en local.
