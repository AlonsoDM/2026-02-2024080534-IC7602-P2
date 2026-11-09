#!/bin/sh
#
# Prueba las imágenes de los dos orígenes propios (T-22) corriendo de verdad en Docker:
#
#   sh scripts/probar-origenes.sh
#
# Construye src/java-api y src/apache (la del API corre JUnit al construirse), las levanta
# en puertos altos y revisa con curl cada verbo del API y cada archivo del sitio. También
# revisa que ninguna arranque sin su variable de puerto. Al final borra los contenedores.

set -eu

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
puerto_java=${PRUEBA_JAVA_PORT:-18080}
puerto_apache=${PRUEBA_APACHE_PORT:-18081}
java=p2-prueba-java-api
apache=p2-prueba-apache
fallos=0

limpiar() {
    docker rm -f "$java" "$apache" >/dev/null 2>&1 || true
}
trap limpiar EXIT
trap 'exit 1' HUP INT TERM

ok() { echo "  ok    $1"; }
mal() { echo "  FALLA $1" >&2; fallos=$((fallos + 1)); }

# Compara un valor con lo esperado: revisar "descripcion" esperado obtenido
revisar() {
    if [ "$2" = "$3" ]; then ok "$1"; else mal "$1: se esperaba '$2' y se obtuvo '$3'"; fi
}

esperar() {
    i=0
    until curl -fs -o /dev/null "$1"; do
        i=$((i + 1))
        if [ "$i" -ge 60 ]; then
            echo "No respondió $1" >&2
            return 1
        fi
        sleep 1
    done
}

codigo() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

echo "Construyendo las imágenes"
docker build -q -t p2/java-api:prueba "$raiz/src/java-api" >/dev/null
docker build -q -t p2/apache:prueba "$raiz/src/apache" >/dev/null

limpiar

echo "Sin variable de puerto no arrancan"
if docker run --rm p2/java-api:prueba >/dev/null 2>&1; then
    mal "java-api arrancó sin JAVA_API_PORT"
else
    ok "java-api no arranca sin JAVA_API_PORT"
fi
docker run -d --name "$apache" p2/apache:prueba >/dev/null
sleep 3
if [ "$(docker inspect -f "{{.State.Running}}" "$apache")" = true ]; then
    mal "apache arrancó sin APACHE_PORT"
else
    ok "apache no arranca sin APACHE_PORT"
fi
docker rm -f "$apache" >/dev/null

docker run -d --name "$java" -e JAVA_API_PORT=9090 -p "127.0.0.1:$puerto_java:9090" p2/java-api:prueba >/dev/null
docker run -d --name "$apache" -e APACHE_PORT=8088 -p "127.0.0.1:$puerto_apache:8088" p2/apache:prueba >/dev/null

api="http://127.0.0.1:$puerto_java"
sitio="http://127.0.0.1:$puerto_apache"
esperar "$api/health"
esperar "$sitio/"

echo "REST API Java en $api (escuchando en JAVA_API_PORT=9090)"
revisar "GET /health" 200 "$(codigo "$api/health")"
revisar "GET /api/productos es JSON" "application/json" \
    "$(curl -s -o /dev/null -w '%{content_type}' "$api/api/productos" | cut -d';' -f1)"
revisar "GET /api/productos trae los 3 de arranque" 3 \
    "$(curl -s "$api/api/productos" | grep -o '"id"' | wc -l | tr -d ' ')"
revisar "POST /api/productos" 201 \
    "$(codigo -X POST -H 'Content-Type: application/json' -d '{"nombre":"Cable","precio":4.5}' "$api/api/productos")"
revisar "GET /api/productos/4" 200 "$(codigo "$api/api/productos/4")"
revisar "PUT /api/productos/4" 200 \
    "$(codigo -X PUT -H 'Content-Type: application/json' -d '{"nombre":"Cable USB","precio":5}' "$api/api/productos/4")"
revisar "PUT cambió el nombre" '"Cable USB"' \
    "$(curl -s "$api/api/productos/4" | grep -o '"Cable USB"')"
revisar "POST con cuerpo inválido" 400 \
    "$(codigo -X POST -H 'Content-Type: application/json' -d '{malo' "$api/api/productos")"
revisar "DELETE /api/productos/4" 204 "$(codigo -X DELETE "$api/api/productos/4")"
revisar "GET de un borrado" 404 "$(codigo "$api/api/productos/4")"

echo "Apache en $sitio (escuchando en APACHE_PORT=8088)"
for archivo in index.html css/estilos.css js/app.js img/logo.png; do
    local_sum=$(cksum < "$raiz/src/apache/sitio/$archivo")
    remoto_sum=$(curl -fs "$sitio/$archivo" | cksum)
    revisar "GET /$archivo llega completo" "$local_sum" "$remoto_sum"
done
for par in index.html:text/html css/estilos.css:text/css js/app.js:text/javascript img/logo.png:image/png; do
    archivo=${par%%:*}
    tipo=${par#*:}
    revisar "Content-Type de /$archivo" "$tipo" \
        "$(curl -s -o /dev/null -w '%{content_type}' "$sitio/$archivo" | cut -d';' -f1)"
done
revisar "GET / sirve index.html" 200 "$(codigo "$sitio/")"
revisar "GET de un archivo que no existe" 404 "$(codigo "$sitio/no-existe.html")"

echo ""
if [ "$fallos" -gt 0 ]; then
    echo "$fallos pruebas fallaron" >&2
    exit 1
fi
echo "Todas las pruebas de los orígenes pasaron"
