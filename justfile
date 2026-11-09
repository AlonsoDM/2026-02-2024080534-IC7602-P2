set dotenv-load := true

release := "p2"
namespace := "p2"
componentes := "interceptor zonal-cache java-api apache"
tf := "terraform -chdir=terraform"
kubeconfig := justfile_directory() / "terraform" / "kubeconfig"

# Muestra las recetas. Va primero para que `just` sin argumentos no toque nada
default:
    @{{just_executable()}} --list --unsorted

# Crea con Terraform la EC2 con k3s y las Elastic IP de las zonas, y deja terraform/kubeconfig (T-24)
infra:
    #!/usr/bin/env bash
    set -euo pipefail
    : "${TF_VAR_ips_equipo:?Falta TF_VAR_ips_equipo en .env (ver .env.example)}"
    {{tf}} init -input=false
    {{tf}} apply -input=false -auto-approve
    echo ""
    {{tf}} output zonas
    echo "Interceptor (nslookup): $({{tf}} output -raw ip_interceptor)"
    echo "kubeconfig: {{kubeconfig}}"
    echo "Siga con: just up"

# Destruye todo lo creado en AWS para no gastar créditos
infra-down:
    #!/usr/bin/env bash
    set -euo pipefail
    : "${TF_VAR_ips_equipo:?Falta TF_VAR_ips_equipo en .env (ver .env.example)}"
    {{tf}} destroy -input=false -auto-approve
    rm -f "{{kubeconfig}}"

# Construye las imágenes del Interceptor, la caché, el API Java y el Apache (cada una corre sus pruebas)
build plataforma="":
    #!/usr/bin/env bash
    set -euo pipefail
    for c in {{componentes}}; do
        echo "== $c"
        docker build {{ if plataforma != "" { "--platform " + plataforma } else { "" } }} -t "p2/$c:local" "src/$c"
    done

# Construye y publica las imágenes en ECR, y las instala en la EC2 con helm upgrade --install
up: certs
    #!/usr/bin/env bash
    set -euo pipefail
    : "${NODE_API_URL:?Falta NODE_API_URL en .env, la URL del REST API Node.js}"
    : "${UI_LOGIN_URL:?Falta UI_LOGIN_URL en .env, el formulario de login de la UI}"
    : "${LLAVE_INTERCEPTOR:?Falta LLAVE_INTERCEPTOR en .env, la llave de componente del Interceptor}"
    : "${LLAVE_CACHE:?Falta LLAVE_CACHE en .env, la llave de componente de las cachés}"
    [ -f "{{kubeconfig}}" ] || { echo "No existe {{kubeconfig}}; corra primero just infra" >&2; exit 1; }
    export KUBECONFIG="{{kubeconfig}}"

    region=$({{tf}} output -raw region)
    registro=$({{tf}} output -raw registro)
    plataforma=$({{tf}} output -raw plataforma)
    valores=$(mktemp)
    trap 'rm -f "$valores"' EXIT
    {{tf}} output -raw valores_helm > "$valores"
    prefijo=$(sed -n 's/^ *"registro": *"\(.*\)"$/\1/p' "$valores")

    # Cada commit es una etiqueta distinta, así helm upgrade reinicia los pods con la imagen nueva
    etiqueta=$(git rev-parse --short HEAD)
    if ! git diff --quiet HEAD -- src; then
        etiqueta="$etiqueta-$(date +%Y%m%d%H%M%S)"
    fi

    echo "== Publicando las imágenes $etiqueta para $plataforma en $prefijo"
    aws ecr get-login-password --region "$region" | docker login --username AWS --password-stdin "$registro"
    for c in {{componentes}}; do
        docker buildx build --platform "$plataforma" -t "$prefijo/$c:$etiqueta" --push "src/$c"
    done

    echo "== Instalando en Kubernetes"
    kubectl create namespace {{namespace}} --dry-run=client -o yaml | kubectl apply -f -
    # El token de ECR dura 12 horas; cada just up lo renueva
    kubectl -n {{namespace}} create secret docker-registry ecr --docker-server="$registro" \
        --docker-username=AWS --docker-password="$(aws ecr get-login-password --region "$region")" \
        --dry-run=client -o yaml | kubectl apply -f -
    helm upgrade --install {{release}} helm/proxy-cache --namespace {{namespace}} \
        -f "$valores" \
        --set imagenes.etiqueta="$etiqueta" \
        --set 'imagenes.pullSecrets[0].name=ecr' \
        --set nodeApi.url="$NODE_API_URL" \
        --set cache.uiLoginUrl="$UI_LOGIN_URL" \
        --set-string llaves.interceptor="$LLAVE_INTERCEPTOR" \
        --set-string llaves.cache="$LLAVE_CACHE" \
        --set-file cache.ca.cert=.certs/rootCA.pem \
        --set-file cache.ca.llave=.certs/rootCA-key.pem \
        --wait --timeout 10m

    {{just_executable()}} zonas
    kubectl -n {{namespace}} get pods,svc -o wide
    echo ""
    echo "Interceptor: $({{tf}} output -raw ip_interceptor)"
    {{tf}} output zonas

# Deja en la tabla zones de Supabase la Elastic IP de cada caché, para que el Interceptor responda las reales
zonas:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ -z "${SUPABASE_DB_URL:-}" ]; then
        echo "Sin SUPABASE_DB_URL no se actualiza la tabla zones. Aplique esto en Supabase:"
        {{tf}} output -raw sql_zonas
        echo ""
        exit 0
    fi
    {{tf}} output -raw sql_zonas | docker run --rm -i postgres:16 psql "$SUPABASE_DB_URL" --no-psqlrc -v ON_ERROR_STOP=1 -q
    echo "Tabla zones actualizada con las Elastic IP"

# Desinstala el release de Helm
down:
    KUBECONFIG="{{kubeconfig}}" helm uninstall {{release}} --namespace {{namespace}} --wait

# Muestra los logs de un componente: interceptor, cache-cr, cache-us, cache-es, cache-gb, java-api o apache
logs componente:
    KUBECONFIG="{{kubeconfig}}" kubectl -n {{namespace}} logs deploy/{{componente}} --tail=200 --timestamps

# Corre las pruebas de Criterion del Interceptor y la caché, y las de JUnit del API Java, dentro de sus imágenes de compilación
test:
    #!/usr/bin/env bash
    set -euo pipefail
    for c in interceptor zonal-cache java-api; do
        echo "== $c"
        docker build --target builder -q "src/$c" >/dev/null
        echo "   pruebas pasaron"
    done

# Revisa el formato de Terraform, lo valida y corre sus pruebas con AWS simulado, sin credenciales
test-infra:
    {{tf}} init -input=false -backend=false >/dev/null
    {{tf}} fmt -check -recursive
    {{tf}} validate
    {{tf}} test
    sh terraform/tests/obtener-kubeconfig.sh

# Lint y pruebas unitarias del Helm Chart (necesita el plugin helm-unittest)
test-helm:
    #!/usr/bin/env bash
    set -euo pipefail
    helm lint --strict helm/proxy-cache -f helm/proxy-cache/tests/valores-prueba.yaml
    if ! helm plugin list | grep -q '^unittest'; then
        echo "Falta el plugin: helm plugin install https://github.com/helm-unittest/helm-unittest" >&2
        exit 1
    fi
    helm unittest helm/proxy-cache

# Instala, actualiza y desinstala el Helm Chart en un k3s dentro de Docker
test-k3s:
    sh scripts/probar-helm.sh

# Prueba las imágenes del API Java y el Apache corriendo en Docker
test-origenes:
    sh scripts/probar-origenes.sh

# Prueba los scripts del justfile (empaquetar) sin Docker ni AWS
test-scripts:
    sh scripts/tests/empaquetar.sh

# Corre la prueba de punta a punta con los cuatro orígenes contra las IP públicas (T-26)
integracion:
    #!/usr/bin/env bash
    set -euo pipefail
    [ -f scripts/prueba-integracion.sh ] || { echo "Todavía no existe scripts/prueba-integracion.sh (T-26)" >&2; exit 1; }
    sh scripts/prueba-integracion.sh

# Prepara la CA local compartida; cada caché emite sus certificados por patrón bajo demanda (T-18)
certs:
    sh scripts/generar-ca.sh

# Aplica migraciones, datos de prueba y tablas IP-to-Country en Supabase (T-11).
# Requiere Docker y SUPABASE_DB_URL en .env (ver .env.example). Se puede repetir.
db:
    #!/usr/bin/env bash
    set -euo pipefail
    : "${SUPABASE_DB_URL:?Falta SUPABASE_DB_URL en .env}"
    docker run --rm -v "{{justfile_directory()}}/supabase:/supabase:ro" postgres:16 \
        bash /supabase/aplicar.sh "$SUPABASE_DB_URL"

# Corre las pruebas de RLS (supabase/tests/) contra la base de SUPABASE_DB_URL. Todo se deshace al final.
db-test:
    #!/usr/bin/env bash
    set -euo pipefail
    : "${SUPABASE_DB_URL:?Falta SUPABASE_DB_URL en .env}"
    docker run --rm -v "{{justfile_directory()}}/supabase/tests:/tests:ro" postgres:16 \
        psql "$SUPABASE_DB_URL" --no-psqlrc -v ON_ERROR_STOP=1 -q -t -A -f /tests/rls_test.sql

# Corre las pruebas del REST API Node.js (T-12): revisa los tipos y corre vitest. No necesita Supabase.
api-test:
    cd src/node-api && npm ci --silent && npm test

# Compila Markdown a PDF en docs/pdf/; sin argumento compila todo docs/ y el README
docs objetivo="all":
    sh scripts/compilar-docs.sh {{objetivo}}

# Genera dist/2026-02-2024080534-IC7602-P2.tar.gz con la documentación compilada, sin .git, node_modules, dist ni secretos
paquete: docs
    sh scripts/empaquetar.sh

# Borra el release, las imágenes locales y la caché de build
limpiar:
    #!/usr/bin/env bash
    set -uo pipefail
    if [ -f "{{kubeconfig}}" ]; then
        KUBECONFIG="{{kubeconfig}}" helm uninstall {{release}} --namespace {{namespace}} 2>/dev/null || true
    fi
    for c in {{componentes}}; do
        for imagen in $(docker image ls --format "{{{{.Repository}}:{{{{.Tag}}" | grep -E "(^|/)(p2|ic7602-p2)/$c:"); do
            docker rmi -f "$imagen"
        done
    done
    docker builder prune -f
