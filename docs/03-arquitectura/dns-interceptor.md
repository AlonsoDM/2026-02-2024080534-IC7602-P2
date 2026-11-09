# DNS Interceptor

Antes recordemos que un mensaje DNS tiene estas 5 secciones:

```text
+---------------------+
|        Header       |
+---------------------+
|       Question      | la pregunta para el name server
+---------------------+
|        Answer       | los registros que responden la pregunta
+---------------------+
|      Authority      | registros que apuntan a una autoridad
+---------------------+
|      Additional     | registros adicionales relacionados
+---------------------+
```

En nuestro caso vamos a revisar el `Header` y `Question`, y para las resoluciones internas también vamos a crear el `Answer`.

## Responsabilidad del componente

Este componente se expone como un servidor DNS en GNU C que escucha en el puerto UDP:53. Recibe los requests DNS que sean enviados a él, decide si se resuelven con las IPs de las cachés zonales del proyecto o con un DNS remoto y devuelve el response al cliente que hizo la solicitud.

El DNS Interceptor es el único componente del proyecto que interpreta y crea mensajes DNS. El REST API Node.js solo recibe el `domain` y el IP del cliente para revisar si el dominio está registrado y seleccionar la caché zonal según el país. Cuando hay que resolver externamente, `upstream.c` forwardea el paquete directamente al DNS remoto por UDP y devuelve el response sin cambiar su contenido.

El puerto `DNS_PORT`, la dirección IPv4 `UPSTREAM_DNS`, el puerto `UPSTREAM_DNS_PORT`, el tiempo de espera `UPSTREAM_TIMEOUT_SECONDS`, la URL `NODE_API_URL`, la llave `NODE_API_KEY`, el tiempo de espera `NODE_API_TIMEOUT_SECONDS` y `DNS_TTL_SECONDS` se reciben por variables de entorno. Los tiempos de espera son enteros positivos en segundos. `DNS_TTL_SECONDS` es un entero no negativo en segundos y se utiliza como TTL común de todos los responses internos de tipo `A`. También maneja múltiples solicitudes al mismo tiempo mediante `pthreads`, con un hilo por paquete.

Se conserva la estructura del Interceptor del P1: `config.c` carga la configuración, `server.c` recibe los paquetes y manda los responses, `handler.c` decide cómo resolver cada solicitud, `dns.c` contiene las operaciones DNS, `api.c` consulta el REST API y `main.c` carga la configuración e inicia el servidor. Se agrega `upstream.c` para la resolución externa. Cada hilo recibe su propia copia del paquete y el IP y puerto del cliente; la configuración se comparte como solo lectura. Las funciones y el flujo DNS se adaptan del P1; la creación de hilos sigue el patrón de `src/servidor.c` de la T2, con un hilo por paquete en lugar de por conexión.

`dns.c` usa las funciones de ldns documentadas oficialmente: `ldns_wire2pkt` lee los paquetes y `ldns_pkt2wire` escribe los responses. Se mantienen `build_internal_response`, `build_format_error_response` y `build_server_failure_response`, además de las funciones de consulta del P1. El llamador libera los mensajes con `ldns_pkt_free` y los responses con `free`. Las preguntas devueltas por `first_question` e `internal_question` pertenecen al mensaje y no se liberan por separado.

## Comunicación con el REST API Node.js

La llamada de esta sección utiliza la URL indicada por `NODE_API_URL` y manda la llave de componente `NODE_API_KEY` en el header `x-api-key`.

`api.c` usa libcurl y cJSON. Cada consulta usa su propio cliente HTTP, con el tiempo de espera de `NODE_API_TIMEOUT_SECONDS`, sin seguir redirects. Solo se acepta HTTP `200` con un JSON del contrato; el cuerpo tiene un límite de 16 KiB. `exists` debe ser booleano y, cuando es `true`, se requieren una zona no vacía y una dirección IPv4 válida.

- `GET /api/interceptor/resolve?domain=&client_ip=` recibe el `domain` y el IP de donde viene la solicitud. En una sola llamada revisa si el dominio está registrado y, si existe, obtiene el país mediante IP-to-Country y devuelve la zona y la IP de su caché.

El endpoint intercambia JSON y mantiene el contrato de la sección 5 del [README](../../README.md#contratos). Si el dominio existe, responde con HTTP `200` y `{"exists": true, "zone": "GB", "address": "203.0.113.24"}`. Si no existe, responde con HTTP `200` y `{"exists": false}`. `exists` es un booleano JSON, no un string. No hay una consulta previa a `/api/exists` ni se manda el paquete DNS al API.

El API usa el `client_ip` enviado por el Interceptor, no el IP de la conexión HTTP. Para probar el comportamiento GEO en desarrollo local, se llama directamente al API con IPs de distintos países en `client_ip`.

La ruta se implementa en `src/node-api/src/routes/interceptor.ts`. Solo intercepta patrones de URLs bajo dominios verificados: un patrón exacto cubre ese host; `*.marca.com` cubre el raíz y todos sus subdominios. Usa `pais_de_ip` y la primera zona cuyo arreglo `countries` contenga el país. Un país desconocido o sin zona devuelve un error HTTP, que el Interceptor convierte en `SERVFAIL`. No hay una zona de respaldo ni países fijos en el código. Si varias zonas incluyen el mismo país, se usa la primera devuelta por la BD.

`src/node-api/test/interceptor-live.mjs` permite comprobar el API en ejecución con IPs de US, ES y GB; los pasos y la evidencia local están en [node-api.md](node-api.md#get-apiinterceptorresolve).

## Proceso

1. Al llegar un request DNS, el Interceptor guarda el IP y puerto del cliente para responderle y, si hace falta, pasar el IP al REST API.

2. Se revisa que el mensaje DNS se pueda leer correctamente. Para extraer el domain, QTYPE y QCLASS se usa ldns, que maneja los tamaños y la compresión de nombres. `dns.c` conserva las funciones del P1, como `get_message`, `is_standard_query`, `first_question` e `internal_question`, usando `ldns_pkt` para el mensaje y `ldns_rr` para las preguntas. `handler.c` llama esas funciones sin interpretar los bytes del paquete. Si el mensaje está mal formado, no se manda al DNS externo; se responde `FORMERR` cuando sea posible recuperar su `Header`. Si no se puede recuperar, no se responde.

3. Una solicitud puede resolverse internamente solo cuando cumple todo esto:

   - `QR = 0` y `OPCODE = 0`, o sea, es un query standard.
   - Tiene una sola `Question`.
   - `QTYPE = A` y `QCLASS = IN`.

   Cualquier otro mensaje se manda al DNS externo.

4. Para resolver externamente, `upstream.c` crea un socket UDP por consulta y manda el paquete original al DNS de `UPSTREAM_DNS` y `UPSTREAM_DNS_PORT`. Espera hasta `UPSTREAM_TIMEOUT_SECONDS`, sin reintentos, y comprueba que el response corresponde a la consulta, incluido su identificador. El response se manda al mismo cliente sin cambiar nada. Si falla la comunicación o se cumple el tiempo de espera sin un response válido, se responde `SERVFAIL`.

5. Para un query interno, el Interceptor extrae el `domain` y llama directamente a `GET /api/interceptor/resolve` con el dominio y el IP del cliente.

   - Si el API responde `exists: false`, se resuelve externamente mediante `upstream.c`.
   - Si responde `exists: true`, se usa la IP de la caché zonal que devuelve en `address`.
   - Si hay un error de comunicación, un error HTTP o un response JSON inválido, se responde `SERVFAIL`. Esto no se debe tratar como si el `domain` no existiera. Un dominio registrado sin una IP de caché válida también produce `SERVFAIL`.

6. Cuando el REST API devuelve una IP, el Interceptor crea el response DNS con ldns. Se conserva la `Question` original y se agrega un registro de tipo `A`, clase `IN`, con la IP seleccionada. El `Header`, los contadores de secciones, los flags y el formato del registro deben seguir los RFC 1035 y RFC 2929 para que el response funcione con clientes como `nslookup`. El TTL de ese registro se toma de `DNS_TTL_SECONDS`; es igual para todos los responses internos y no forma parte del response del API.

## Response interno

Para que un response interno funcione bien:

- Se conserva el identificador de la consulta y la `Question` original.
- `QR` indica que ahora es un response, `OPCODE` sigue siendo el de un query standard y `RCODE` indica que no hubo error.
- En `Answer` va el único registro `A` que se seleccionó. `Authority` y `Additional` quedan vacíos.
- Los contadores del `Header` deben coincidir con lo que realmente se está mandando.
- Como las IPs internas salen de nuestra BD, el response se marca como authoritative. También se indica que hay recursion disponible porque los dominios que no son nuestros se resuelven con el DNS remoto.

Para buscar el `domain` se puede normalizar el nombre, pero no se debe cambiar la `Question` original que se devuelve al cliente.

## Alcance

Nuestro DNS interno solo trabaja con requests IPv4 de tipo `A`. Los otros tipos de request y los dominios que no existen se mandan al DNS externo. En el P2 solo se seleccionan cachés por geolocalización; no se mantienen las políticas ni la revisión de IPs saludables del P1.

Desde la raíz del repositorio se construye y ejecuta así en Linux:

```bash
docker build -t p2-interceptor:debug src/interceptor
docker run --rm --network host --env-file .env.example p2-interceptor:debug
```

El Dockerfile compila con `-Wall -Wextra -Werror` y corre las pruebas de Criterion antes de crear la imagen final, que solo lleva el ejecutable y sus librerías y corre con un usuario sin privilegios. `.env.example` reúne los valores de desarrollo: el Interceptor escucha en UDP `5300`, usa Google DNS (`8.8.8.8:53`) y espera un API en `http://127.0.0.1:8787`. Para personalizarlos, copiarlo a `.env` y usar `--env-file .env`. Se debe cambiar `NODE_API_URL` y, si corresponde, `NODE_API_KEY` para usar el API desplegado. La llave de desarrollo es la misma del seed de Supabase. `--network host` permite acceder al API local desde el contenedor.

Desde otra terminal se pueden hacer estas consultas:

```bash
dig @127.0.0.1 -p 5300 example.com A
dig @127.0.0.1 -p 5300 example.com AAAA
nslookup -port=5300 example.com 127.0.0.1
```

## Bibliografía

- [ldns — diseño oficial](https://www.nlnetlabs.nl/documentation/ldns/design.html)
- [ldns — lectura de mensajes DNS](https://www.nlnetlabs.nl/documentation/ldns/wire2host_8h.html)
- [ldns — escritura de mensajes DNS](https://www.nlnetlabs.nl/documentation/ldns/host2wire_8h.html)
- [libcurl — documentación oficial](https://curl.se/libcurl/c/libcurl.html)
- [cJSON — documentación oficial](https://github.com/DaveGamble/cJSON)
- [POSIX pthread_create — documentación oficial](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_create.html)
- [DNS Interceptor del P1 — diseño adaptado](https://github.com/JoseZum/2026-02-2024080534-IC7602-P1/blob/main/docs/03-arquitectura/dns-interceptor.md)
- [RFC 1035 — Domain Names: Implementation and Specification](https://datatracker.ietf.org/doc/html/rfc1035#section-1)
- [RFC 2929 — Domain Name System (DNS) IANA Considerations](https://datatracker.ietf.org/doc/html/rfc2929)
