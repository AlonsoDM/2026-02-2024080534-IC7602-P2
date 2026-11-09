#!/bin/sh
#
# Prueba de punta a punta (T-26). Corre desde la compu del equipo contra las IP públicas:
#
#   just integracion
#
# 1. Resuelve cada host con el Interceptor (nslookup) y revisa que la respuesta sea la IP de
#    una caché zonal, y que un dominio que no es nuestro se reenvía al DNS externo.
# 2. Por esa caché pide el REST API Java, el Apache (HTML, CSS, JS y PNG), el sitio externo
#    HTTP y el sitio externo HTTPS: la primera vez es un miss y la segunda un hit (X-Cache: HIT).
# 3. Revisa el 401 sin x-api-key o con una llave inválida, el 200 con la buena, y el 302 al
#    formulario de la UI sin sesión.
# 4. Llena el disco de una URL con más de lo que cabe y revisa con kubectl que en ninguna caché
#    el disco de una URL pasa su cache_size_bytes.
#
# Lee .env como el justfile. Variables:
#   NODE_API_URL, LLAVE_CACHE   para leer los límites de cada URL de GET /api/cache/config
#   UI_LOGIN_URL                para revisar a dónde manda el 302
#   IP_INTERCEPTOR              por defecto, la salida ip_interceptor de Terraform
#   IP_CACHE                    si se da, no se resuelve con el Interceptor y se usa esta IP (pruebas locales)
#   CA_CERT                     por defecto .certs/rootCA.pem
#   LLAVE_URL_JAVA              API Key de seguro.java-api.test, por defecto la de supabase/seed.sql
#   KUBECONFIG, NAMESPACE       para revisar el disco; por defecto terraform/kubeconfig y p2
#   PETICIONES_LIMITE           cuántos PNG distintos se piden para llenar el disco (300, unos 2.4 MB)

set -eu

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
tf="terraform -chdir=$raiz/terraform"

api=${NODE_API_URL:?Falta NODE_API_URL}
llave_cache=${LLAVE_CACHE:?Falta LLAVE_CACHE}
ui_login=${UI_LOGIN_URL:-}
ca=${CA_CERT:-$raiz/.certs/rootCA.pem}
llave_java=${LLAVE_URL_JAVA:-dev-java-key}
ns=${NAMESPACE:-p2}
export KUBECONFIG="${KUBECONFIG:-$raiz/terraform/kubeconfig}"
peticiones_limite=${PETICIONES_LIMITE:-300}
ip_cache_fija=${IP_CACHE:-}

trabajo=$(mktemp -d)
trap 'rm -rf -- "$trabajo"' EXIT
fallos=0
# Cada corrida pide rutas nuevas, así la primera petición siempre es un miss aunque se repita la prueba
corrida=$(date +%s)-$$

ok() { echo "  ok    $1"; }
mal() { echo "  FALLA $1" >&2; fallos=$((fallos + 1)); }
revisar() {
    if [ "$2" = "$3" ]; then ok "$1"; else mal "$1: se esperaba '$2' y se obtuvo '$3'"; fi
}

for herramienta in curl nslookup kubectl; do
    command -v "$herramienta" >/dev/null 2>&1 || { echo "Hace falta $herramienta" >&2; exit 1; }
done
[ -f "$ca" ] || { echo "No existe la CA $ca; corra just certs" >&2; exit 1; }

if [ -n "$ip_cache_fija" ]; then
    ip_dns=${IP_INTERCEPTOR:-}
else
    ip_dns=${IP_INTERCEPTOR:-$($tf output -raw ip_interceptor)}
fi

# La primera dirección A de la respuesta, sin la línea del servidor (que trae #53)
resolver() {
    nslookup -type=A "$1" "$ip_dns" 2>/dev/null |
        awk '/^Name:/ { respuesta = 1 } respuesta && /^Address/ { print $NF; exit }'
}

# IP de la caché para un host: la que responde el Interceptor, o IP_CACHE en pruebas locales
ip_de() {
    if [ -n "$ip_cache_fija" ]; then echo "$ip_cache_fija"; else resolver "$1"; fi
}

# peticion esquema host ruta [opciones de curl...]: deja los headers en $trabajo/h y devuelve el código
peticion() {
    esquema=$1 host=$2 ruta=$3
    shift 3
    if [ "$esquema" = https ]; then puerto=443; else puerto=80; fi
    curl -s -o "$trabajo/cuerpo" -D "$trabajo/h" -w '%{http_code}' --max-time 30 --cacert "$ca" \
        --resolve "$host:$puerto:$(ip_de "$host")" "$@" "$esquema://$host$ruta" || echo 000
}

es_hit() {
    tr -d '\r' < "$trabajo/h" | grep -qi '^x-cache: *hit$' && echo HIT || echo MISS
}

# Primera petición miss y segunda hit, las dos con 200
miss_y_hit() {
    nombre=$1 esquema=$2 host=$3 ruta=$4
    codigo=$(peticion "$esquema" "$host" "$ruta")
    revisar "$nombre: primera petición 200" 200 "$codigo"
    revisar "$nombre: primera petición es un miss" MISS "$(es_hit)"
    codigo=$(peticion "$esquema" "$host" "$ruta")
    revisar "$nombre: segunda petición 200" 200 "$codigo"
    revisar "$nombre: segunda petición sale de la caché" HIT "$(es_hit)"
}

echo "1. DNS Interceptor"
if [ -n "$ip_cache_fija" ]; then
    echo "  --    se usa IP_CACHE=$ip_cache_fija sin resolver con el Interceptor"
else
    zonas=$($tf output zonas)
    for host in java-api.test apache.test externo-http.test externo-https.test seguro.java-api.test privado.apache.test; do
        ip=$(resolver "$host")
        if [ -n "$ip" ] && printf '%s\n' "$zonas" | grep -q "\"$ip\""; then
            ok "$host resuelve a la caché $ip"
        else
            mal "$host debe resolver a la IP de una caché zonal, resolvió '$ip'"
        fi
    done
fi
if [ -n "$ip_dns" ]; then
    externa=$(resolver example.com)
    if [ -n "$externa" ] && ! printf '%s\n' "${zonas:-}" | grep -q "\"$externa\""; then
        ok "example.com se reenvía al DNS externo ($externa)"
    else
        mal "example.com debe resolverse afuera, resolvió '$externa'"
    fi
fi

echo "2. Miss y hit con los cuatro orígenes"
miss_y_hit "REST API Java" http java-api.test "/api/productos?prueba=$corrida"
miss_y_hit "Apache HTML" http apache.test "/index.html?prueba=$corrida"
miss_y_hit "Apache CSS" http apache.test "/css/estilos.css?prueba=$corrida"
miss_y_hit "Apache JS" http apache.test "/js/app.js?prueba=$corrida"
miss_y_hit "Apache PNG" http apache.test "/img/logo.png?prueba=$corrida"
miss_y_hit "Sitio externo HTTP" http externo-http.test "/get?prueba=$corrida"
miss_y_hit "Sitio externo HTTPS" https externo-https.test "/get?prueba=$corrida"

codigo=$(peticion http java-api.test /api/productos -X POST -H 'Content-Type: application/json' \
    -d '{"nombre":"Prueba de integración","precio":1}')
revisar "POST al API Java pasa por la caché" 201 "$codigo"
revisar "POST no se guarda" MISS "$(es_hit)"

echo "3. Autenticación"
revisar "x-api-key ausente" 401 "$(peticion http seguro.java-api.test "/api/productos?prueba=$corrida")"
revisar "x-api-key inválida" 401 \
    "$(peticion http seguro.java-api.test "/api/productos?prueba=$corrida" -H 'x-api-key: llave-que-no-existe')"
revisar "x-api-key válida" 200 \
    "$(peticion http seguro.java-api.test "/api/productos?prueba=$corrida" -H "x-api-key: $llave_java")"
revisar "Usuario y contraseña sin sesión" 302 "$(peticion http privado.apache.test /index.html)"
destino=$(tr -d '\r' < "$trabajo/h" | sed -n 's/^[Ll]ocation: //p')
case $destino in
    "$ui_login"*callback=*) ok "el 302 manda al formulario con el callback ($destino)" ;;
    *) mal "el 302 debe mandar a ${ui_login:-UI_LOGIN_URL} con callback, mandó '$destino'" ;;
esac

echo "4. El disco nunca pasa el límite"
curl -s --max-time 20 -H "x-api-key: $llave_cache" "$api/api/cache/config" > "$trabajo/config"
# "id":5,"pattern":"externo-http.test",...,"cache_size_bytes":2097152 -> 5 2097152 externo-http.test
grep -o '"id":[0-9]*,"pattern":"[^"]*"[^}]*"cache_size_bytes":[0-9]*' "$trabajo/config" |
    sed 's/"id":\([0-9]*\),"pattern":"\([^"]*\)".*"cache_size_bytes":\([0-9]*\)/\1 \3 \2/' > "$trabajo/limites"
if [ ! -s "$trabajo/limites" ]; then
    mal "no se pudo leer GET /api/cache/config"
else
    limite=$(awk '$3 == "externo-http.test" { print $2 }' "$trabajo/limites")
    echo "  --    pidiendo $peticiones_limite recursos distintos a externo-http.test (límite $limite bytes)"
    # Cada PNG de httpbin pesa unos 8 KB y cada query distinta es un recurso distinto en la caché
    i=0
    guardables=0
    while [ "$i" -lt "$peticiones_limite" ]; do
        [ "$(peticion http externo-http.test "/image/png?relleno=$corrida-$i")" = 200 ] && guardables=$((guardables + 1))
        i=$((i + 1))
    done
    echo "  --    $guardables de $peticiones_limite respondieron 200"

    mas_lleno=0
    caches=$(kubectl -n "$ns" get deploy -l app.kubernetes.io/name=zonal-cache -o name)
    [ -n "$caches" ] || mal "no se encontraron cachés en el namespace $ns"
    for cache in $caches; do
        directorio=$(kubectl -n "$ns" exec "$cache" -- printenv CACHE_DIR)
        while read -r id tope patron; do
            usado=$(kubectl -n "$ns" exec "$cache" -- sh -c \
                "find '$directorio/$id' -type f -printf '%s\n' 2>/dev/null | awk '{ s += \$1 } END { print s + 0 }'")
            if [ "$usado" -le "$tope" ]; then
                ok "${cache#deployment.apps/} $patron usa $usado de $tope bytes"
            else
                mal "${cache#deployment.apps/} $patron usa $usado bytes y su límite es $tope"
            fi
            if [ "$patron" = externo-http.test ] && [ "$usado" -gt "$mas_lleno" ]; then
                mas_lleno=$usado
            fi
        done < "$trabajo/limites"
    done
    # Si la caché que recibió el relleno quedó casi vacía, la prueba del límite no probó nada
    if [ "$mas_lleno" -ge $((limite / 2)) ]; then
        ok "el relleno sí llenó externo-http.test hasta $mas_lleno bytes sin pasar $limite"
    else
        mal "el relleno debió llenar externo-http.test y solo llegó a $mas_lleno de $limite bytes"
    fi
fi

echo ""
if [ "$fallos" -gt 0 ]; then
    echo "$fallos pruebas fallaron" >&2
    exit 1
fi
echo "La prueba de punta a punta pasó"
