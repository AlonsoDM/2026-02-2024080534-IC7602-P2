# Prueba de punta a punta

`scripts/prueba-integracion.sh` prueba el sistema completo como lo usaría un cliente: desde la compu del equipo, contra las IP públicas que dio Terraform, resuelve cada host con el DNS Interceptor y le pide los recursos a la Zonal Cache que respondió. Se corre con:

```bash
just integracion
```

Necesita que `just infra` y `just up` hayan terminado, y las variables `NODE_API_URL`, `LLAVE_CACHE` y `UI_LOGIN_URL` de `.env`. Usa `curl`, `nslookup` y `kubectl`, y la CA de `.certs/rootCA.pem` para el HTTPS.

## Qué revisa

| Paso | Qué hace | Qué se espera |
| --- | --- | --- |
| 1. DNS | `nslookup <host> <IP del Interceptor>` para los seis hosts de los datos de prueba | Cada uno resuelve a la Elastic IP de una caché zonal (una de la salida `zonas` de Terraform) |
| | `nslookup example.com <IP del Interceptor>` | Responde una IP que no es de ninguna caché: el Interceptor la reenvió al DNS externo |
| 2. Miss y hit | Dos `GET` iguales por la caché a `java-api.test/api/productos`, a `apache.test` (`index.html`, `css/estilos.css`, `js/app.js` e `img/logo.png`), a `externo-http.test/get` y a `externo-https.test/get` por HTTPS | Las dos con `200`; la primera sin `X-Cache: HIT` y la segunda con `X-Cache: HIT` |
| | Un `POST` al API Java por la caché | `201`, y no se guarda |
| 3. `x-api-key` | `seguro.java-api.test` sin header, con una llave inválida y con `dev-java-key` | `401`, `401` y `200` |
| Usuario y contraseña | `privado.apache.test` sin cookie de sesión | `302` con `Location` al formulario de `UI_LOGIN_URL`, con `url` y `callback` |
| 4. Límite del disco | Lee `cache_size_bytes` de cada URL en `GET /api/cache/config`, pide 300 PNG distintos de unos 8 KB a `externo-http.test` (unos 2.4 MB, más que su límite de 2 MB) y suma con `kubectl exec` los archivos de cada URL en `CACHE_DIR` de cada caché | Ninguna URL pasa su límite en ninguna caché, y la que se llenó quedó al menos a la mitad, para que la prueba sí haya puesto el disco bajo presión |

Cada corrida agrega un `?prueba=<hora>-<pid>` a las rutas: así la primera petición siempre es un miss aunque la prueba se repita antes de que venza el TTL.

## Variables

| Variable | Por defecto | Para qué |
| --- | --- | --- |
| `NODE_API_URL` y `LLAVE_CACHE` | De `.env` | Leer los límites de cada URL |
| `UI_LOGIN_URL` | De `.env` | Revisar a dónde manda el `302` |
| `IP_INTERCEPTOR` | Salida `ip_interceptor` de Terraform | Servidor DNS de `nslookup` |
| `IP_CACHE` | Vacía | Si se da, los hosts registrados no se resuelven con el Interceptor y se usa esta IP (pruebas locales) |
| `CA_CERT` | `.certs/rootCA.pem` | CA para el HTTPS |
| `LLAVE_URL_JAVA` | `dev-java-key` | API Key válida de `seguro.java-api.test` |
| `KUBECONFIG` y `NAMESPACE` | `terraform/kubeconfig` y `p2` | Revisar el disco de las cachés |
| `PETICIONES_LIMITE` | `300` | Cuántos PNG distintos se piden para llenar el disco |

## Probarla sin AWS

`scripts/tests/prueba-integracion-k3s.sh` corre la misma prueba contra un k3s dentro de Docker:

```bash
NODE_API_URL=https://zonal-api.duran-munozalonso.workers.dev sh scripts/tests/prueba-integracion-k3s.sh
```

Construye las cuatro imágenes, instala el Helm Chart con una zona en la IP de prueba `10.99.0.11` y el Interceptor en `10.99.0.10`, y corre `prueba-integracion.sh` desde un contenedor que comparte la red del k3s, igual que un cliente que llega a las Elastic IP. Desde ahí el cliente tiene una IP privada sin país, así que el Interceptor no puede geolocalizarlo: los hosts registrados se piden a la caché con `IP_CACHE`. El reenvío de `example.com` sí pasa por el Interceptor. La geolocalización se prueba aparte, llamando al REST API con IP de varios países (T-17).
