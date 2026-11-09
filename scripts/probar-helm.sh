#!/bin/sh
#
# Prueba el Helm Chart (T-23) en un k3s de verdad que corre dentro de Docker, sin AWS:
#
#   sh scripts/probar-helm.sh
#
# 1. Levanta rancher/k3s en un contenedor y le agrega IP de prueba que hacen de IP secundarias.
# 2. Construye las cuatro imágenes y las carga en el k3s.
# 3. Instalación limpia: espera a que cada Deployment quede disponible y prueba desde un pod
#    el Apache y el API Java por la caché, por su Service y por la externalIP de la zona,
#    el 401 sin x-api-key y que el Interceptor reenvía un dominio que no es nuestro.
# 4. Actualización: cambia valores con helm upgrade y revisa que los pods los tomen.
# 5. Desinstalación: revisa que no quede nada del release.
#
# Las cachés piden su configuración al REST API Node.js, así que hace falta red. Por defecto
# se usa el API de NODE_API_URL con las llaves de desarrollo de supabase/seed.sql.

set -eu

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
helm=${HELM:-helm}
k3s=${PRUEBA_K3S_IMAGEN:-rancher/k3s:v1.31.4-k3s1}
contenedor=p2-prueba-k3s
puerto_api=${PRUEBA_K3S_PUERTO:-16443}
api_url=${NODE_API_URL:?Falta NODE_API_URL, la URL del REST API Node.js}
llave_interceptor=${LLAVE_INTERCEPTOR:-dev-interceptor-key}
llave_cache=${LLAVE_CACHE:-dev-cache-key}
release=p2
ns=p2-prueba
fallos=0

trabajo=$(mktemp -d)
export KUBECONFIG="$trabajo/kubeconfig"

limpiar() {
    docker rm -f "$contenedor" >/dev/null 2>&1 || true
    rm -rf -- "$trabajo"
}
trap limpiar EXIT
trap 'exit 1' HUP INT TERM

ok() { echo "  ok    $1"; }
mal() { echo "  FALLA $1" >&2; fallos=$((fallos + 1)); }
revisar() {
    if [ "$2" = "$3" ]; then ok "$1"; else mal "$1: se esperaba '$2' y se obtuvo '$3'"; fi
}

# Corre un comando en un pod de curl dentro del clúster y devuelve su salida
dentro() {
    kubectl -n "$ns" exec curl -- "$@" 2>/dev/null || true
}

echo "Levantando k3s en Docker ($k3s)"
docker rm -f "$contenedor" >/dev/null 2>&1 || true
docker run -d --privileged --name "$contenedor" -p "127.0.0.1:$puerto_api:6443" "$k3s" \
    server --disable traefik --disable servicelb >/dev/null

i=0
until docker exec "$contenedor" kubectl get nodes 2>/dev/null | grep -q ' Ready'; do
    i=$((i + 1))
    [ "$i" -lt 90 ] || { echo "k3s no arrancó" >&2; exit 1; }
    sleep 2
done
docker exec "$contenedor" cat /etc/rancher/k3s/k3s.yaml |
    sed "s#https://127.0.0.1:6443#https://127.0.0.1:$puerto_api#" > "$KUBECONFIG"

# Hacen el papel de la IP principal y de las secundarias de la EC2
for ip in 10.99.0.10 10.99.0.11 10.99.0.12; do
    docker exec "$contenedor" ip addr add "$ip/32" dev eth0
done

echo "Construyendo y cargando las imágenes"
for componente in interceptor zonal-cache java-api apache; do
    docker build -q -t "p2/$componente:prueba" "$raiz/src/$componente" >/dev/null
    docker save "p2/$componente:prueba" | docker exec -i "$contenedor" ctr images import - >/dev/null
done

# CA de prueba solo para esta corrida
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj "/CN=CA de prueba" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout "$trabajo/rootCA-key.pem" -out "$trabajo/rootCA.pem" >/dev/null 2>&1

cat > "$trabajo/valores.yaml" <<EOF
imagenes:
  registro: p2
  etiqueta: prueba
  pullPolicy: Never
nodeApi:
  url: $api_url
cache:
  uiLoginUrl: https://ui.ejemplo.test/login
  tamanoDisco: 64Mi
  zonas:
    - { codigo: CR, externalIP: 10.99.0.11 }
    - { codigo: US, externalIP: 10.99.0.12 }
interceptor:
  externalIPs: [10.99.0.10]
EOF

instalar() {
    "$helm" upgrade --install "$release" "$raiz/helm/proxy-cache" --namespace "$ns" --create-namespace \
        -f "$trabajo/valores.yaml" \
        --set-string "llaves.interceptor=$llave_interceptor,llaves.cache=$llave_cache" \
        --set-file "cache.ca.cert=$trabajo/rootCA.pem,cache.ca.llave=$trabajo/rootCA-key.pem" \
        --wait --timeout 5m "$@" >/dev/null
}

echo "Instalación limpia"
if instalar; then ok "helm install terminó con todo disponible"; else mal "helm install"; fi
for deploy in interceptor cache-cr cache-us java-api apache; do
    revisar "Deployment $deploy disponible" 1 \
        "$(kubectl -n "$ns" get deploy "$deploy" -o jsonpath='{.status.availableReplicas}' 2>/dev/null)"
done
revisar "Service cache-cr escucha su externalIP" 10.99.0.11 \
    "$(kubectl -n "$ns" get svc cache-cr -o jsonpath='{.spec.externalIPs[0]}')"
revisar "CACHE_ZONE de cache-us" US \
    "$(kubectl -n "$ns" get deploy cache-us -o jsonpath='{.spec.template.spec.containers[0].env[?(@.name=="CACHE_ZONE")].value}')"

kubectl -n "$ns" run curl --image=curlimages/curl:8.11.1 --restart=Never --command -- sleep 3600 >/dev/null
kubectl -n "$ns" wait --for=condition=Ready pod/curl --timeout=120s >/dev/null

revisar "Apache directo por su Service" 200 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' http://apache/)"
revisar "API Java directo por su Service" 200 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' http://java-api:8080/api/productos)"
revisar "Apache por la caché CR" 200 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' -H 'Host: apache.test' http://cache-cr/)"
revisar "API Java por la externalIP de la caché US" 200 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' -H 'Host: java-api.test' http://10.99.0.12/api/productos)"
revisar "Segunda petición sale de la caché" HIT \
    "$(dentro curl -s -o /dev/null -D - -H 'Host: java-api.test' http://10.99.0.12/api/productos |
        tr -d '\r' | sed -n 's/^[Xx]-[Cc]ache: //p')"
revisar "URL con x-api-key sin llave" 401 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' -H 'Host: seguro.java-api.test' http://cache-cr/api/productos)"
revisar "HTTPS por la caché con la CA de prueba" 200 \
    "$(kubectl -n "$ns" get secret "$release-ca" -o jsonpath='{.data.rootCA\.pem}' | base64 -d > "$trabajo/ca.pem" &&
        kubectl -n "$ns" cp "$trabajo/ca.pem" curl:/tmp/ca.pem >/dev/null 2>&1 &&
        dentro curl -s -o /dev/null -w '%{http_code}' --cacert /tmp/ca.pem \
            --resolve externo-https.test:443:10.99.0.11 https://externo-https.test/get)"
# Un dominio que no es nuestro se reenvía al DNS externo y vuelve con su respuesta
revisar "Interceptor reenvía example.com" NOERROR \
    "$(dentro sh -c 'nslookup -type=a example.com 10.99.0.10 >/dev/null 2>&1 && echo NOERROR')"

echo "Actualización"
if instalar --set interceptor.dnsTtlSeconds=120 --set cache.configRefreshSeconds=15; then
    ok "helm upgrade terminó con todo disponible"
else
    mal "helm upgrade"
fi
revisar "Revisión del release" 2 "$("$helm" -n "$ns" history "$release" -o json | grep -o '"revision":[0-9]*' | tail -1 | cut -d: -f2)"
revisar "El Interceptor tomó DNS_TTL_SECONDS nuevo" 120 \
    "$(kubectl -n "$ns" exec deploy/interceptor -- printenv DNS_TTL_SECONDS 2>/dev/null)"
revisar "La caché tomó CONFIG_REFRESH_SECONDS nuevo" 15 \
    "$(kubectl -n "$ns" exec deploy/cache-cr -- printenv CONFIG_REFRESH_SECONDS 2>/dev/null)"
revisar "Apache sigue respondiendo por la caché" 200 \
    "$(dentro curl -s -o /dev/null -w '%{http_code}' -H 'Host: apache.test' http://cache-cr/)"

echo "Desinstalación"
kubectl -n "$ns" delete pod curl --wait=false >/dev/null
if "$helm" uninstall "$release" --namespace "$ns" --wait >/dev/null; then
    ok "helm uninstall"
else
    mal "helm uninstall"
fi
revisar "No quedan objetos del release" 0 \
    "$(kubectl -n "$ns" get deploy,svc,secret -l "app.kubernetes.io/instance=$release" -o name | wc -l | tr -d ' ')"

echo ""
if [ "$fallos" -gt 0 ]; then
    echo "$fallos pruebas fallaron" >&2
    exit 1
fi
echo "Todas las pruebas del Helm Chart pasaron"
