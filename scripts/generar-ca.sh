#!/bin/sh

set -eu

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
salida="$raiz/.certs"

if ! command -v openssl >/dev/null 2>&1; then
    echo "Hace falta OpenSSL para generar la CA local." >&2
    exit 1
fi

umask 077
mkdir -p "$salida"

cert="$salida/rootCA.pem"
llave="$salida/rootCA-key.pem"

if [ -e "$cert" ] && [ -e "$llave" ]; then
    echo "Se conserva la CA local de $salida"
    exit 0
fi

if [ -e "$cert" ] || [ -e "$llave" ]; then
    echo "La CA local está incompleta: hacen falta rootCA.pem y rootCA-key.pem. No se sobrescriben los archivos." >&2
    exit 1
fi

temporal=$(mktemp -d "$salida/.ca.XXXXXX")
trap 'rm -rf -- "$temporal"' EXIT
trap 'exit 1' HUP INT TERM

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 3650 \
    -subj "/CN=P2 CA Local" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout "$temporal/rootCA-key.pem" \
    -out "$temporal/rootCA.pem"

chmod 600 "$temporal/rootCA-key.pem"
chmod 644 "$temporal/rootCA.pem"
mv "$temporal/rootCA-key.pem" "$llave"
mv "$temporal/rootCA.pem" "$cert"

echo "CA local generada en $salida"
