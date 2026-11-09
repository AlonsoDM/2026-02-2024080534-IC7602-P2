#!/bin/sh
#
# Lo llama Terraform (terraform_data.kubeconfig) después de crear la EC2. Espera a que el
# user_data publique el kubeconfig en SSM Parameter Store y lo guarda en RUTA_KUBECONFIG.
# Usa la AWS CLI con las mismas credenciales de Terraform. Se puede correr a mano:
#
#   REGION=us-east-1 PARAMETRO=/ic7602-p2/kubeconfig RUTA_KUBECONFIG=terraform/kubeconfig sh terraform/obtener-kubeconfig.sh

set -eu

: "${REGION:?Falta REGION}"
: "${PARAMETRO:?Falta PARAMETRO}"
: "${RUTA_KUBECONFIG:?Falta RUTA_KUBECONFIG}"
intentos=${INTENTOS:-120}
espera=${ESPERA_SEGUNDOS:-5}

if ! command -v aws >/dev/null 2>&1; then
    echo "Hace falta la AWS CLI para traer el kubeconfig." >&2
    exit 1
fi

echo "Esperando el kubeconfig en $PARAMETRO (k3s tarda unos minutos en instalarse)"

i=0
while :; do
    valor=$(aws ssm get-parameter --region "$REGION" --name "$PARAMETRO" --with-decryption \
        --query Parameter.Value --output text 2>/dev/null || true)

    # Terraform crea el parámetro con "pendiente"; la EC2 lo cambia por el kubeconfig
    case $valor in
        apiVersion:*) break ;;
    esac

    i=$((i + 1))
    if [ "$i" -ge "$intentos" ]; then
        echo "La EC2 no publicó el kubeconfig después de $((intentos * espera)) segundos." >&2
        echo "Revise /var/log/p2-arranque.log en la EC2 (EC2 > Instancia > Monitorear > Obtener registro del sistema)." >&2
        exit 1
    fi
    sleep "$espera"
done

umask 077
mkdir -p "$(dirname "$RUTA_KUBECONFIG")"
printf '%s\n' "$valor" > "$RUTA_KUBECONFIG"
echo "kubeconfig guardado en $RUTA_KUBECONFIG"
