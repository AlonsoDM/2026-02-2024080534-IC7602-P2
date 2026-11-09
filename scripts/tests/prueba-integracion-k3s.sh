#!/bin/sh
#
# Corre scripts/prueba-integracion.sh contra un k3s dentro de Docker, sin AWS:
#
#   NODE_API_URL=<url del REST API> sh scripts/tests/prueba-integracion-k3s.sh
#
# Instala el Helm Chart como en scripts/probar-helm.sh y corre la prueba desde un contenedor
# que comparte la red del k3s, así llega a las IP de prueba igual que un cliente llega a las
# Elastic IP. Desde aquí el Interceptor no puede geolocalizar (el cliente tiene IP privada), por
# eso los hosts registrados se piden a la IP de la caché con IP_CACHE; el reenvío de un dominio
# externo sí pasa por el Interceptor.

set -eu

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
helm=${HELM:-helm}
k3s=${PRUEBA_K3S_IMAGEN:-rancher/k3s:v1.31.4-k3s1}
contenedor=p2-prueba-integracion-k3s
api_url=${NODE_API_URL:?Falta NODE_API_URL, la URL del REST API Node.js}
llave_interceptor=${LLAVE_INTERCEPTOR:-dev-interceptor-key}
llave_cache=${LLAVE_CACHE:-dev-cache-key}
ui_login=https://ui.ejemplo.test/login
ns=p2

trabajo=$(mktemp -d)
limpiar() {
    docker rm -f "$contenedor" >/dev/null 2>&1 || true
    rm -rf -- "$trabajo"
}
trap limpiar EXIT
trap 'exit 1' HUP INT TERM

echo "Levantando k3s en Docker"
docker rm -f "$contenedor" >/dev/null 2>&1 || true
docker run -d --privileged --name "$contenedor" "$k3s" server --disable traefik --disable servicelb >/dev/null
until docker exec "$contenedor" kubectl get nodes 2>/dev/null | grep -q ' Ready'; do sleep 2; done
for ip in 10.99.0.10 10.99.0.11; do
    docker exec "$contenedor" ip addr add "$ip/32" dev eth0
done
docker exec "$contenedor" cat /etc/rancher/k3s/k3s.yaml > "$trabajo/kubeconfig"

echo "Construyendo y cargando las imágenes"
for componente in interceptor zonal-cache java-api apache; do
    docker build -q -t "p2/$componente:prueba" "$raiz/src/$componente" >/dev/null
    docker save "p2/$componente:prueba" | docker exec -i "$contenedor" ctr images import - >/dev/null
done

openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj "/CN=CA de prueba" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout "$trabajo/rootCA-key.pem" -out "$trabajo/rootCA.pem" >/dev/null 2>&1

# Una sola zona para que la prueba sepa a qué caché pedir; la de Helm prueba varias
echo "Instalando el chart"
docker run --rm --network "container:$contenedor" -v "$raiz/helm:/helm:ro" -v "$trabajo:/trabajo" \
    -e KUBECONFIG=/trabajo/kubeconfig alpine/helm:3.16.3 \
    upgrade --install p2 /helm/proxy-cache --namespace "$ns" --create-namespace \
    --set imagenes.registro=p2 --set imagenes.etiqueta=prueba --set imagenes.pullPolicy=Never \
    --set nodeApi.url="$api_url" --set cache.uiLoginUrl="$ui_login" --set cache.tamanoDisco=64Mi \
    --set 'cache.zonas[0].codigo=CR' --set 'cache.zonas[0].externalIP=10.99.0.11' \
    --set 'interceptor.externalIPs={10.99.0.10}' \
    --set-string llaves.interceptor="$llave_interceptor" --set-string llaves.cache="$llave_cache" \
    --set-file cache.ca.cert=/trabajo/rootCA.pem --set-file cache.ca.llave=/trabajo/rootCA-key.pem \
    --wait --timeout 5m >/dev/null

echo "Corriendo la prueba de punta a punta"
docker run --rm --network "container:$contenedor" -v "$raiz:/repo:ro" -v "$trabajo:/trabajo" \
    -e KUBECONFIG=/trabajo/kubeconfig -e NODE_API_URL="$api_url" -e LLAVE_CACHE="$llave_cache" \
    -e UI_LOGIN_URL="$ui_login" -e CA_CERT=/trabajo/rootCA.pem \
    -e IP_CACHE=10.99.0.11 -e IP_INTERCEPTOR=10.99.0.10 -e NAMESPACE="$ns" \
    -e PETICIONES_LIMITE="${PETICIONES_LIMITE:-300}" \
    alpine:3.20 sh -c 'apk add -q --no-cache curl bind-tools kubectl >/dev/null && sh /repo/scripts/prueba-integracion.sh'
