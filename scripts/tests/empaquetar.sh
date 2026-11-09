#!/bin/sh
#
# Prueba scripts/empaquetar.sh con un repositorio falso que tiene secretos y basura:
#   sh scripts/tests/empaquetar.sh

set -eu

aqui=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
trabajo=$(mktemp -d)
trap 'rm -rf -- "$trabajo"' EXIT
fallos=0
ok() { echo "  ok    $1"; }
mal() { echo "  FALLA $1" >&2; fallos=$((fallos + 1)); }

repo="$trabajo/mi-clon"
mkdir -p "$repo/docs/pdf" "$repo/.git" "$repo/src/ui/node_modules/x" "$repo/.certs" "$repo/dist" \
    "$repo/terraform/.terraform" "$repo/src/zonal-cache/build" "$repo/src/java-api/target"
touch "$repo/README.md" "$repo/.env.example" "$repo/terraform/main.tf" "$repo/src/node-api/.dev.vars" 2>/dev/null ||
    { mkdir -p "$repo/src/node-api" && touch "$repo/src/node-api/.dev.vars"; }
echo secreto > "$repo/.env"
echo llave > "$repo/.certs/rootCA-key.pem"
echo estado > "$repo/terraform/terraform.tfstate"
echo estado > "$repo/terraform/terraform.tfstate.backup"
echo kube > "$repo/terraform/kubeconfig"
echo viejo > "$repo/dist/viejo.tar.gz"
touch "$repo/.git/HEAD" "$repo/src/ui/node_modules/x/i.js" "$repo/src/zonal-cache/build/pruebas" "$repo/src/java-api/target/a.jar"

if RAIZ="$repo" SALIDA="$trabajo/salida" sh "$aqui/../empaquetar.sh" >/dev/null 2>&1; then
    mal "empaquetó sin PDF en docs/pdf"
else
    ok "sin PDF compilados pide correr just docs"
fi

echo pdf > "$repo/docs/pdf/proyecto.pdf"
if ! RAIZ="$repo" SALIDA="$trabajo/salida" sh "$aqui/../empaquetar.sh" >/dev/null; then
    mal "no generó el paquete"
fi
paquete="$trabajo/salida/2026-02-2024080534-IC7602-P2.tar.gz"
tar -tzf "$paquete" > "$trabajo/lista"

for esperado in 2026-02-2024080534-IC7602-P2/README.md 2026-02-2024080534-IC7602-P2/.env.example \
    2026-02-2024080534-IC7602-P2/docs/pdf/proyecto.pdf 2026-02-2024080534-IC7602-P2/terraform/main.tf; do
    if grep -qx "$esperado" "$trabajo/lista"; then ok "lleva $esperado"; else mal "falta $esperado"; fi
done

for prohibido in '/\.env$' '/\.git/' 'node_modules' '/\.certs' 'tfstate' 'terraform/kubeconfig' '/dist/' \
    '/\.terraform/' '\.dev\.vars' '/build/' '/target/'; do
    if grep -q "$prohibido" "$trabajo/lista"; then mal "no debe llevar $prohibido"; else ok "no lleva $prohibido"; fi
done

if grep -q 'mi-clon' "$trabajo/lista"; then
    mal "la carpeta de arriba debe llamarse como el proyecto"
else
    ok "la carpeta de arriba se llama como el proyecto aunque el clon se llame distinto"
fi

[ ! -e "$paquete.tmp" ] && ok "no deja el temporal" || mal "dejó el temporal"

[ "$fallos" -eq 0 ] || exit 1
