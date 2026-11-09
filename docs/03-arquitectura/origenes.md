# Orígenes de prueba: REST API Java y Apache

El enunciado pide probar la Zonal Cache con cuatro orígenes. Dos son sitios externos (`httpbin.org` por HTTP y por HTTPS) y los otros dos son nuestros y corren dentro del clúster: un REST API en Java y un Apache con un sitio estático. Los datos de prueba de Supabase ya apuntan a ellos por su nombre de `Service` de Kubernetes:

| Patrón | Origen | Autenticación |
| --- | --- | --- |
| `java-api.test` | `http://java-api:8080` | Ninguna |
| `seguro.java-api.test` | `http://java-api:8080` | `x-api-key` |
| `apache.test` | `http://apache:80` | Ninguna |
| `privado.apache.test` | `http://apache:80` | Usuario y contraseña |

Ninguno de los dos sabe que hay una caché adelante: responden como cualquier servidor HTTP y la caché decide qué guarda.

## REST API Java (`src/java-api/`)

Java 21 con [Javalin](https://javalin.io/) 6, el mismo microframework del DNS API del P1. Javalin corre sobre Jetty, que atiende cada petición en su propio hilo.

| Archivo | Qué hace |
| --- | --- |
| `Main.java` | Lee la configuración, carga tres productos de arranque y levanta el servidor |
| `Configuracion.java` | Lee `JAVA_API_PORT` y no deja arrancar si falta o no es un puerto válido |
| `App.java` | Registra las rutas y valida el id y el cuerpo de cada petición |
| `RepositorioProductos.java` | Guarda los productos en un `ConcurrentHashMap`, con ids de un `AtomicLong` |
| `Producto.java` | El record `{ id, nombre, precio }` |

### Rutas

| Método y ruta | Respuesta |
| --- | --- |
| `GET /health` | `200 {"estado":"ok"}`, la usan las sondas de Kubernetes |
| `GET /api/productos` | `200` con la lista ordenada por id |
| `GET /api/productos/{id}` | `200` con el producto, o `404` |
| `POST /api/productos` | `201` con el producto creado y el header `Location`, o `400` |
| `PUT /api/productos/{id}` | `200` con el producto reemplazado, `400`, o `404` si no existe (PUT no crea) |
| `DELETE /api/productos/{id}` | `204` sin cuerpo, o `404` |

POST y PUT reciben `{"nombre": "Mouse", "precio": 12.5}`. Se responde `400` con `{"error": "..."}` si el JSON no se puede leer, si falta el nombre o viene vacío, o si el precio falta o es negativo. Un id que no es un entero positivo también da `400`.

Todas las respuestas son `application/json`. La ruta no tiene extensión, así que la caché toma el tipo `json` del `Content-Type` y lo compara con los tipos de la URL, donde `java-api.test` tiene `json` con TTL de 30 segundos. Solo se guardan los `GET` con `200`; POST, PUT y DELETE se reenvían siempre al origen.

El recurso vive en memoria: al reiniciar el pod se vuelve a los tres productos de arranque. El enunciado pide un API simple con los cuatro verbos, no persistencia.

Cada petición queda en el log del pod con su método, ruta, código y duración. Así se comprueba un hit: la segunda petición por la caché no aparece en el log del API.

### Configuración

| Variable | Ejemplo | Para qué |
| --- | --- | --- |
| `JAVA_API_PORT` | `8080` | Puerto donde escucha. Obligatoria, entre 1 y 65535 |

### Imagen

`Dockerfile` multietapa: `maven:3.9-eclipse-temurin-21` baja las dependencias en una capa aparte, corre `mvn package` (que corre las pruebas de JUnit) y arma un solo jar con el plugin shade. La imagen final es `eclipse-temurin:21-jre` con ese jar y un usuario sin privilegios. Si una prueba falla, la imagen no se construye, igual que con `make test` en las imágenes de C.

## Apache (`src/apache/`)

La imagen oficial `httpd:2.4` con un sitio estático en `sitio/`:

| Archivo | Tipo | TTL en `apache.test` |
| --- | --- | ---: |
| `index.html` | `html` | 60 s |
| `css/estilos.css` | `css` | 300 s |
| `js/app.js` | `js` | 300 s |
| `img/logo.png` | `png` | 600 s |

Así se prueba que la caché toma el tipo de la extensión y le aplica a cada archivo su propio TTL.

El `Dockerfile` cambia en `httpd.conf` la línea `Listen 80` por `Listen ${APACHE_PORT}`. Apache expande `${VARIABLE}` con el entorno del proceso al leer su configuración, de modo que el puerto sale de la variable sin plantillas ni scripts de arranque. Si `APACHE_PORT` no existe, Apache no arranca. También fija `ServerName localhost` para que no avise en cada arranque.

| Variable | Ejemplo | Para qué |
| --- | --- | --- |
| `APACHE_PORT` | `80` | Puerto donde escucha. Obligatoria |

## Pruebas

| Prueba | Cómo se corre | Qué revisa |
| --- | --- | --- |
| JUnit del API Java | Al construir la imagen, o `docker build src/java-api` | `ConfiguracionTest`: el puerto se lee bien y falta o un valor que no sirve no deja arrancar. `RepositorioProductosTest`: ids seguidos, PUT que no crea, DELETE y 8 hilos creando a la vez sin repetir ids. `ProductosApiTest`: cada ruta con su código, el `Location` del POST, el `Content-Type` JSON y cada caso de `400` y `404`, levantando el servidor real con `javalin-testtools` |
| Imágenes en Docker | `sh scripts/probar-origenes.sh` | Construye las dos imágenes, comprueba que no arrancan sin su variable de puerto, las levanta en puertos que no son los de siempre y prueba con `curl` los cuatro verbos del API y que cada archivo del sitio llega completo y con su `Content-Type` |

Para probar a mano:

```bash
docker build -t p2/java-api src/java-api
docker run --rm -e JAVA_API_PORT=8080 -p 8080:8080 p2/java-api

curl http://localhost:8080/api/productos
curl -X POST -H 'Content-Type: application/json' -d '{"nombre":"Cable","precio":4.5}' http://localhost:8080/api/productos
curl -X PUT -H 'Content-Type: application/json' -d '{"nombre":"Cable USB","precio":5}' http://localhost:8080/api/productos/4
curl -X DELETE -i http://localhost:8080/api/productos/4
```

```bash
docker build -t p2/apache src/apache
docker run --rm -e APACHE_PORT=8081 -p 8081:8081 p2/apache

curl -i http://localhost:8081/
curl -I http://localhost:8081/img/logo.png
```
