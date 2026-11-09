# Pruebas

Este documento junta todas las pruebas del proyecto: qué revisa cada una, cómo correrla y qué dio la última vez que se corrió. Hay tres niveles:

1. **Pruebas unitarias** de cada componente, que no necesitan red ni AWS y corren dentro de las imágenes de Docker al construirlas. Si una falla, la imagen no se construye.
2. **Pruebas de despliegue**, que instalan las imágenes y el Helm Chart en un Kubernetes de verdad (k3s dentro de Docker) y le hacen peticiones reales.
3. **La prueba de punta a punta**, que corre desde la compu del equipo contra las IP públicas de AWS ([punta-a-punta.md](punta-a-punta.md)).

Todas se corren con `just`. La guía de instalación ([docs/01-instalacion](../01-instalacion/README.md)) explica cómo instalar `just`, Docker y lo demás.

## Resumen

| Prueba | Componente | Comando | Cantidad | Resultado |
| --- | --- | --- | ---: | --- |
| Criterion del Interceptor | DNS Interceptor | `just test` | 22 | Pasan |
| Criterion de la caché | Zonal Cache | `just test` | 81 | Pasan con el arreglo de la sección [Zonal Cache](#zonal-cache) |
| JUnit del API Java | REST API Java | `just test` | 15 | Pasan |
| vitest del REST API Node.js | REST API Node.js | `just api-test` | 111 | Pasan (T-14) |
| RLS en Supabase | Supabase | `just db-test` | 1 script | Pasa (T-11) |
| Imágenes de los orígenes | REST API Java y Apache | `just test-origenes` | 24 | Pasan |
| Terraform con AWS simulado | Infraestructura | `just test-infra` | 11 + 4 | Pasan |
| Helm unittest | Helm Chart | `just test-helm` | 25 | Pasan |
| Helm en k3s | Todo lo de Kubernetes | `just test-k3s` | 23 | Pasan |
| Empaquetado | `just paquete` | `just test-scripts` | 18 | Pasan |
| Punta a punta en k3s | Todo, sin geolocalización | `sh scripts/tests/prueba-integracion-k3s.sh` | 45 | Pasan |
| Punta a punta en AWS | Todo | `just integracion` | — | Pendiente de correr en la EC2 |

## 1. Pruebas unitarias

### DNS Interceptor

`src/interceptor/tests/`, con Criterion. Las escribió PERSONA 3 en la T-16 y la T-17, y el diseño está en [dns-interceptor.md](../03-arquitectura/dns-interceptor.md). Revisan:

- El parseo del `Header` y la `Question` con mensajes reales y mal formados, límites y punteros de compresión.
- `FORMERR` y `SERVFAIL`.
- El armado del response `A` y la lectura de las variables de entorno.

`just test` solo dice si pasaron. Para ver el detalle de cada prueba:

```bash
docker build --progress=plain --target builder src/interceptor
```

```
[====] Synthesis: Tested: 22 | Passing: 22 | Failing: 0 | Crashing: 0
```

### Zonal Cache

`src/zonal-cache/tests/`, con Criterion. Las escribieron PERSONA 1, 2 y 3 en sus tareas. Revisan el servidor, el parseo HTTP, la configuración, el reenvío al origen, el disco, `x-api-key`, la sesión y TLS, y también se corrieron con AddressSanitizer. El detalle está en [zonal-cache.md](../03-arquitectura/zonal-cache.md).

```bash
docker build --progress=plain --target builder src/zonal-cache
```

```
[====] Synthesis: Tested: 81 | Passing: 81 | Failing: 0 | Crashing: 0
```

> En `main`, desde el merge de la T-05, dos pruebas no compilan o fallan: `tests/test_cache.c` llama `http_leer_peticion` con un `int` en lugar de un `Conexion *`, y `entorno_valido()` de `tests/test_config.c` no pone `CACHE_DIR`. Con esos dos arreglos de una línea pasan las 81. Mientras no estén en `main`, `docker build src/zonal-cache` falla.

### REST API Java

`src/java-api/src/test/`, con JUnit 5 y `javalin-testtools`, que levanta el servidor real en un puerto libre. Corren con `mvn package` dentro del `Dockerfile`. El detalle está en [origenes.md](../03-arquitectura/origenes.md).

| Clase | Pruebas | Qué revisa |
| --- | ---: | --- |
| `ConfiguracionTest` | 3 | `JAVA_API_PORT` se lee bien; sin ella, vacía, con letras, `0`, `65536` o `80.5` no arranca |
| `RepositorioProductosTest` | 4 | Ids seguidos, PUT que no crea, DELETE que dice si existía y 8 hilos creando 2000 productos sin repetir ids |
| `ProductosApiTest` | 8 | Cada ruta con su código, el `Location` del POST, el `Content-Type` JSON, y cada caso de `400` y `404` |

```
[INFO] Tests run: 8, Failures: 0, Errors: 0, Skipped: 0 -- in cr.ac.tec.ic7602.javaapi.ProductosApiTest
[INFO] Tests run: 3, Failures: 0, Errors: 0, Skipped: 0 -- in cr.ac.tec.ic7602.javaapi.ConfiguracionTest
[INFO] Tests run: 4, Failures: 0, Errors: 0, Skipped: 0 -- in cr.ac.tec.ic7602.javaapi.RepositorioProductosTest
[INFO] Tests run: 15, Failures: 0, Errors: 0, Skipped: 0
[INFO] BUILD SUCCESS
```

### REST API Node.js y Supabase

Las pruebas del Worker (`src/node-api/test/`, vitest con un Supabase falso) y las de RLS (`supabase/tests/rls_test.sql`) están documentadas en [node-api.md](../03-arquitectura/node-api.md) y [supabase.md](../03-arquitectura/supabase.md).

```bash
just api-test   # no necesita Supabase
just db-test    # necesita SUPABASE_DB_URL; todo se deshace al final
```

### Terraform

`terraform/tests/`, con `terraform test` y un `mock_provider` de AWS: no necesitan credenciales ni crean nada. El detalle está en [infraestructura.md](../03-arquitectura/infraestructura.md#pruebas).

- `plan.tftest.hcl` (10 pruebas): una IP secundaria fija y una Elastic IP por zona; el security group abre solo 53/udp, 80 y 443 a todos y el 6443 a las IP del equipo; ECR; el parámetro cifrado; AWS Academy; 2 zonas en `arm64`; las salidas para Helm y Supabase; y que se rechazan `0.0.0.0/0`, un CIDR mal escrito, más de 4 zonas y zonas repetidas.
- `arranque.tftest.hcl` (1 prueba): el user_data ya armado.
- `obtener-kubeconfig.sh` (4 pruebas, con una AWS CLI falsa).

```bash
just test-infra
```

```
Success! The configuration is valid.
tests/arranque.tftest.hcl... pass
tests/plan.tftest.hcl... pass
Success! 11 passed, 0 failed.
  ok    espera mientras dice pendiente y guarda el kubeconfig
  ok    el kubeconfig solo lo lee su dueño
  ok    pide el parámetro descifrado
  ok    se rinde después de INTENTOS y no deja archivo
```

### Helm Chart

`helm/proxy-cache/tests/`, con el plugin [helm-unittest](https://github.com/helm-unittest/helm-unittest) (`helm plugin install https://github.com/helm-unittest/helm-unittest`). Revisan cada objeto que arma el chart sin instalarlo: los objetos por zona y su `CACHE_ZONE`, cada variable de entorno, los volúmenes, `externalIPs` con `externalTrafficPolicy: Local`, los nombres de los orígenes y los errores por valores que faltan o no sirven. El detalle está en [helm.md](../03-arquitectura/helm.md#pruebas).

```bash
just test-helm
```

```
1 chart(s) linted, 0 chart(s) failed
 PASS  cachés zonales           proxy-cache/tests/cache_test.yaml
 PASS  DNS Interceptor          proxy-cache/tests/interceptor_test.yaml
 PASS  orígenes propios         proxy-cache/tests/origenes_test.yaml
 PASS  secretos                 proxy-cache/tests/secretos_test.yaml
Tests:       25 passed, 25 total
```

Para comprobar que las pruebas sí detectan errores, se cambió a propósito `CACHE_ZONE` por `CACHE_ZON` y `externalTrafficPolicy: Local` por `Cluster`, y fallaron las tres pruebas que los revisan.

### Empaquetado

`scripts/tests/empaquetar.sh` arma un repositorio falso con `.env`, la llave de la CA, estado de Terraform, `kubeconfig`, `.git`, `node_modules`, `dist`, `build` y `target`, y revisa que el `tar.gz` no lleve nada de eso pero sí el README, `.env.example`, el código y los PDF.

```bash
just test-scripts
```

## 2. Pruebas de despliegue

### Imágenes de los orígenes

`scripts/probar-origenes.sh` construye las imágenes del API Java y del Apache, revisa que no arrancan sin su variable de puerto y las levanta en puertos que no son los de siempre (`JAVA_API_PORT=9090` y `APACHE_PORT=8088`) para comprobar que el puerto de verdad sale de la variable.

```bash
just test-origenes
```

```
Sin variable de puerto no arrancan
  ok    java-api no arranca sin JAVA_API_PORT
  ok    apache no arranca sin APACHE_PORT
REST API Java en http://127.0.0.1:18080 (escuchando en JAVA_API_PORT=9090)
  ok    GET /health
  ok    GET /api/productos es JSON
  ok    GET /api/productos trae los 3 de arranque
  ok    POST /api/productos
  ok    GET /api/productos/4
  ok    PUT /api/productos/4
  ok    PUT cambió el nombre
  ok    POST con cuerpo inválido
  ok    DELETE /api/productos/4
  ok    GET de un borrado
Apache en http://127.0.0.1:18081 (escuchando en APACHE_PORT=8088)
  ok    GET /index.html llega completo
  ok    GET /css/estilos.css llega completo
  ok    GET /js/app.js llega completo
  ok    GET /img/logo.png llega completo
  ok    Content-Type de /index.html
  ok    Content-Type de /css/estilos.css
  ok    Content-Type de /js/app.js
  ok    Content-Type de /img/logo.png
  ok    GET / sirve index.html
  ok    GET de un archivo que no existe

Todas las pruebas de los orígenes pasaron
```

### Helm Chart en k3s

`scripts/probar-helm.sh` levanta `rancher/k3s` en Docker y le agrega las IP `10.99.0.10` a `10.99.0.12`, que hacen el papel de la IP principal y las secundarias de la EC2. Carga las cuatro imágenes e instala el chart con dos zonas contra el REST API desplegado en Cloudflare. Después lo actualiza y lo desinstala.

```bash
NODE_API_URL=https://zonal-api.duran-munozalonso.workers.dev just test-k3s
```

```
Instalación limpia
  ok    helm install terminó con todo disponible
  ok    Deployment interceptor disponible
  ok    Deployment cache-cr disponible
  ok    Deployment cache-us disponible
  ok    Deployment java-api disponible
  ok    Deployment apache disponible
  ok    Service cache-cr escucha su externalIP
  ok    CACHE_ZONE de cache-us
  ok    Apache directo por su Service
  ok    API Java directo por su Service
  ok    Apache por la caché CR
  ok    API Java por la externalIP de la caché US
  ok    Segunda petición sale de la caché
  ok    URL con x-api-key sin llave
  ok    HTTPS por la caché con la CA de prueba
  ok    Interceptor reenvía example.com
Actualización
  ok    helm upgrade terminó con todo disponible
  ok    Revisión del release
  ok    El Interceptor tomó DNS_TTL_SECONDS nuevo
  ok    La caché tomó CONFIG_REFRESH_SECONDS nuevo
  ok    Apache sigue respondiendo por la caché
Desinstalación
  ok    helm uninstall
  ok    No quedan objetos del release
Todas las pruebas del Helm Chart pasaron
```

## 3. Prueba de punta a punta

Está explicada en [punta-a-punta.md](punta-a-punta.md). Contra AWS se corre con `just integracion` después de `just infra` y `just up`. Sin AWS se corre contra el k3s local; ahí el cliente tiene una IP privada sin país, así que los hosts registrados se piden directo a la IP de la caché y solo el reenvío de `example.com` pasa por el Interceptor:

```bash
NODE_API_URL=https://zonal-api.duran-munozalonso.workers.dev sh scripts/tests/prueba-integracion-k3s.sh
```

```
1. DNS Interceptor
  --    se usa IP_CACHE=10.99.0.11 sin resolver con el Interceptor
  ok    example.com se reenvía al DNS externo (104.20.23.154)
2. Miss y hit con los cuatro orígenes
  ok    REST API Java: primera petición 200
  ok    REST API Java: primera petición es un miss
  ok    REST API Java: segunda petición 200
  ok    REST API Java: segunda petición sale de la caché
  ok    Apache HTML: primera petición es un miss
  ok    Apache HTML: segunda petición sale de la caché
  ok    Apache CSS: primera petición es un miss
  ok    Apache CSS: segunda petición sale de la caché
  ok    Apache JS: primera petición es un miss
  ok    Apache JS: segunda petición sale de la caché
  ok    Apache PNG: primera petición es un miss
  ok    Apache PNG: segunda petición sale de la caché
  ok    Sitio externo HTTP: primera petición es un miss
  ok    Sitio externo HTTP: segunda petición sale de la caché
  ok    Sitio externo HTTPS: primera petición es un miss
  ok    Sitio externo HTTPS: segunda petición sale de la caché
  ok    POST al API Java pasa por la caché
  ok    POST no se guarda
3. Autenticación
  ok    x-api-key ausente
  ok    x-api-key inválida
  ok    x-api-key válida
  ok    Usuario y contraseña sin sesión
  ok    el 302 manda al formulario con el callback (https://ui.ejemplo.test/login?url=4&callback=http%3A%2F%2Fprivado.apache.test%2F__auth%2Fcallback%3Fnext%3D%252Findex.html)
4. El disco nunca pasa el límite
  --    pidiendo 300 recursos distintos a externo-http.test (límite 2097152 bytes)
  --    300 de 300 respondieron 200
  ok    cache-cr java-api.test usa 126 de 1048576 bytes
  ok    cache-cr seguro.java-api.test usa 182 de 1048576 bytes
  ok    cache-cr apache.test usa 2748 de 5242880 bytes
  ok    cache-cr privado.apache.test usa 0 de 5242880 bytes
  ok    cache-cr externo-http.test usa 2095562 de 2097152 bytes
  ok    cache-cr seguro.externo-http.test usa 0 de 2097152 bytes
  ok    cache-cr externo-https.test usa 253 de 2097152 bytes
  ok    cache-cr privado.externo-https.test usa 0 de 2097152 bytes
  ok    el relleno sí llenó externo-http.test hasta 2095562 bytes sin pasar 2097152
La prueba de punta a punta pasó
```

Se omiten las líneas de `200` de cada petición; en total son 45 pruebas. Después de 300 PNG de unos 8 KB, unos 2.4 MB, `externo-http.test` quedó en 2 095 562 bytes, a 1590 bytes de su límite de 2 MB y sin pasarlo.

## 4. Cómo reproducir cada prueba a mano

Estos pasos son para alguien que quiere ver con sus propios ojos cada funcionalidad, con `nslookup` y `curl`, sin los scripts. Se necesita que `just infra` y `just up` hayan terminado. Primero se guardan las IP en variables:

```bash
INTERCEPTOR=$(terraform -chdir=terraform output -raw ip_interceptor)
nslookup java-api.test "$INTERCEPTOR"
```

```
Server:		54.80.12.34
Address:	54.80.12.34#53

Name:	java-api.test
Address: 3.214.10.5
```

La `Address` de abajo es la caché de la zona de su país. Guárdela:

```bash
CACHE=3.214.10.5
```

### DNS Interceptor

| Qué se prueba | Comando | Qué se espera |
| --- | --- | --- |
| Un dominio registrado responde la caché de la zona | `nslookup java-api.test $INTERCEPTOR` | La Elastic IP de una zona de `terraform -chdir=terraform output zonas` |
| Otro host registrado del mismo dominio | `nslookup seguro.java-api.test $INTERCEPTOR` | La misma IP |
| Un dominio que no es nuestro se reenvía sin tocar | `nslookup example.com $INTERCEPTOR` y `nslookup example.com 8.8.8.8` | Las dos dan la misma respuesta |
| Otros tipos de consulta se reenvían | `nslookup -type=AAAA example.com $INTERCEPTOR` | La IPv6 de `example.com` |
| La zona depende del país del cliente | `curl "$NODE_API_URL/api/interceptor/resolve?domain=java-api.test&client_ip=81.2.69.160" -H "x-api-key: $LLAVE_INTERCEPTOR"` con IP de distintos países | `zone` cambia: `GB` para `81.2.69.160`, `US` para `8.8.8.8` (T-17) |

### Zonal Cache

| Qué se prueba | Comando | Qué se espera |
| --- | --- | --- |
| Miss y hit con el API Java | `curl -i --resolve java-api.test:80:$CACHE http://java-api.test/api/productos` dos veces | `200`; la segunda trae `X-Cache: HIT` y no aparece en `just logs java-api` |
| Cada tipo de archivo del Apache | `curl -I --resolve apache.test:80:$CACHE http://apache.test/img/logo.png` dos veces, y lo mismo con `index.html`, `css/estilos.css` y `js/app.js` | La segunda con `X-Cache: HIT` |
| Sitio externo HTTP | `curl -i --resolve externo-http.test:80:$CACHE http://externo-http.test/get` dos veces | La segunda con `X-Cache: HIT` |
| Sitio externo HTTPS con SSL termination | `curl -i --cacert .certs/rootCA.pem --resolve externo-https.test:443:$CACHE https://externo-https.test/get` dos veces | El certificado se acepta sin `-k`; la segunda con `X-Cache: HIT` |
| Lo que no es GET no se guarda | `curl -i --resolve java-api.test:80:$CACHE -X POST -H 'Content-Type: application/json' -d '{"nombre":"Cable","precio":4.5}' http://java-api.test/api/productos` | `201` sin `X-Cache` |
| PUT y DELETE llegan al origen | `curl -i --resolve java-api.test:80:$CACHE -X PUT -H 'Content-Type: application/json' -d '{"nombre":"Cable USB","precio":5}' http://java-api.test/api/productos/4` y `curl -i --resolve java-api.test:80:$CACHE -X DELETE http://java-api.test/api/productos/4` | `200` y `204` |
| Un host sin URL configurada | `curl -i --resolve otro.test:80:$CACHE http://otro.test/` | `404` |
| `x-api-key` ausente | `curl -i --resolve seguro.java-api.test:80:$CACHE http://seguro.java-api.test/api/productos` | `401` |
| `x-api-key` inválida | El mismo con `-H 'x-api-key: no-sirve'` | `401` |
| `x-api-key` válida | El mismo con `-H 'x-api-key: dev-java-key'` | `200` |
| Usuario y contraseña sin sesión | `curl -i --resolve privado.apache.test:80:$CACHE http://privado.apache.test/` | `302` con `Location` al formulario de la UI y un `callback` |
| El disco no pasa el límite | `kubectl -n p2 exec deploy/cache-cr -- du -sb /var/cache/zonal-cache` después de muchas peticiones | Cada carpeta (una por id de URL) queda por debajo de su `cache_size_bytes` |

Con `export KUBECONFIG=$PWD/terraform/kubeconfig`, `just logs cache-cr` muestra cada petición que llegó a esa caché y `just logs java-api` cada una que llegó al origen.

### REST API Java y Apache sin la caché

Se pueden probar solos en la compu, sin Kubernetes, como explica [origenes.md](../03-arquitectura/origenes.md#pruebas):

```bash
docker build -t p2/java-api src/java-api
docker run --rm -e JAVA_API_PORT=8080 -p 8080:8080 p2/java-api
curl http://localhost:8080/api/productos
```
