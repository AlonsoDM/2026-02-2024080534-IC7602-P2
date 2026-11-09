#!/bin/sh
#
# Genera el tar.gz de la entrega en dist/ (lo llama `just paquete` después de `just docs`):
#
#   sh scripts/empaquetar.sh
#
# Lleva el repositorio con la documentación ya compilada en docs/pdf/, pero sin .git,
# node_modules, dist ni nada que tenga secretos: .env, la CA local, el estado de Terraform
# y el kubeconfig. RAIZ y SALIDA se pueden cambiar para las pruebas.

set -eu

raiz=${RAIZ:-$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)}
nombre=${NOMBRE_PAQUETE:-2026-02-2024080534-IC7602-P2}
salida=${SALIDA:-$raiz/dist}
paquete="$salida/$nombre.tar.gz"

if ! ls "$raiz"/docs/pdf/*.pdf >/dev/null 2>&1; then
    echo "No hay PDF en docs/pdf/. Corra primero just docs." >&2
    exit 1
fi

mkdir -p "$salida"
temporal="$paquete.tmp"
trap 'rm -f -- "$temporal"' EXIT

# El directorio de arriba del tar se llama como el proyecto, aunque el clon tenga otro nombre.
# Para eso se archiva un enlace con ese nombre y tar -h lo sigue; el repositorio no tiene otros enlaces
padre=$(dirname -- "$raiz")
carpeta=$(basename -- "$raiz")
enlace=""
if [ "$carpeta" != "$nombre" ]; then
    enlace=$(mktemp -d)
    ln -s "$raiz" "$enlace/$nombre"
    padre=$enlace
    carpeta=$nombre
fi

tar -czhf "$temporal" -C "$padre" \
    --exclude=.git \
    --exclude=node_modules \
    --exclude="$carpeta/dist" \
    --exclude=.env \
    --exclude=.dev.vars \
    --exclude=.certs \
    --exclude=.wrangler \
    --exclude=.terraform \
    --exclude='*.tfstate' \
    --exclude='*.tfstate.*' \
    --exclude='*.tfvars' \
    --exclude="$carpeta/terraform/kubeconfig" \
    --exclude=.claude \
    --exclude=.DS_Store \
    --exclude=target \
    --exclude=build \
    "$carpeta/"

[ -z "$enlace" ] || rm -rf -- "$enlace"
mv "$temporal" "$paquete"
echo "Paquete en $paquete ($(du -h "$paquete" | cut -f1))"
