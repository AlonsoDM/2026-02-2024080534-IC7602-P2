#!/bin/sh
#
# Prueba obtener-kubeconfig.sh con una AWS CLI falsa, sin tocar AWS:
#   sh terraform/tests/obtener-kubeconfig.sh

set -eu

aqui=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
trabajo=$(mktemp -d)
trap 'rm -rf -- "$trabajo"' EXIT
fallos=0

# La AWS CLI falsa responde "pendiente" las primeras PENDIENTES veces y después el kubeconfig
mkdir "$trabajo/bin"
cat > "$trabajo/bin/aws" <<'FALSO'
#!/bin/sh
echo "$*" >> "$TRABAJO/llamadas"
n=$(wc -l < "$TRABAJO/llamadas")
if [ "$n" -le "$PENDIENTES" ]; then
    echo pendiente
else
    printf 'apiVersion: v1\nclusters:\n- cluster:\n    server: https://198.51.100.10:6443\n'
fi
FALSO
chmod +x "$trabajo/bin/aws"

correr() {
    rm -f "$trabajo/llamadas" "$trabajo/kube/config"
    PATH="$trabajo/bin:$PATH" TRABAJO="$trabajo" PENDIENTES=$1 INTENTOS=$2 ESPERA_SEGUNDOS=0 \
        REGION=us-east-1 PARAMETRO=/ic7602-p2/kubeconfig RUTA_KUBECONFIG="$trabajo/kube/config" \
        sh "$aqui/../obtener-kubeconfig.sh" >/dev/null 2>&1
}

if correr 3 10 && grep -q 'server: https://198.51.100.10:6443' "$trabajo/kube/config"; then
    echo "  ok    espera mientras dice pendiente y guarda el kubeconfig"
else
    echo "  FALLA no guardó el kubeconfig" >&2; fallos=$((fallos + 1))
fi

permisos=$(ls -l "$trabajo/kube/config" | cut -c1-10)
if [ "$permisos" = "-rw-------" ]; then
    echo "  ok    el kubeconfig solo lo lee su dueño"
else
    echo "  FALLA permisos $permisos" >&2; fallos=$((fallos + 1))
fi

if grep -q -- '--name /ic7602-p2/kubeconfig --with-decryption' "$trabajo/llamadas"; then
    echo "  ok    pide el parámetro descifrado"
else
    echo "  FALLA no pidió el parámetro correcto" >&2; fallos=$((fallos + 1))
fi

if correr 50 4; then
    echo "  FALLA terminó bien aunque nunca llegó el kubeconfig" >&2; fallos=$((fallos + 1))
elif [ -e "$trabajo/kube/config" ]; then
    echo "  FALLA dejó un kubeconfig sin haberlo recibido" >&2; fallos=$((fallos + 1))
else
    echo "  ok    se rinde después de INTENTOS y no deja archivo"
fi

[ "$fallos" -eq 0 ] || exit 1
