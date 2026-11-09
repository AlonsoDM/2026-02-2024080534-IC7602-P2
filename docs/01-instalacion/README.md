# Guía de instalación

Esta guía lleva de una computadora sin nada instalado hasta el sistema corriendo en AWS y probado con `nslookup` y `curl`. Está pensada para alguien con conocimientos básicos de terminal: cada paso dice qué comando correr, qué debería salir y qué hacer si no sale.

Al final van a quedar corriendo:

| Dónde | Qué | Cómo se instala |
| --- | --- | --- |
| Supabase | La base de datos con los dominios, URLs, llaves, zonas y la tabla IP-to-Country | `just db` |
| Cloudflare | El REST API Node.js (y la UI cuando exista) | Solo, con cada merge a `main` |
| AWS | Una EC2 con k3s y una Elastic IP por zona | `just infra` |
| Kubernetes en la EC2 | El DNS Interceptor, una Zonal Cache por zona, el REST API Java y el Apache | `just up` |

Todo lo de AWS y Kubernetes se crea con dos comandos. No hay que entrar a la consola de AWS ni conectarse por SSH a la EC2.

> La EC2 y las Elastic IP cobran mientras existen. Cuando termine de probar, corra `just infra-down` (sección 9).

## 1. Programas que hay que instalar

| Programa | Versión | Para qué |
| --- | --- | --- |
| Git | Cualquiera | Clonar el repositorio |
| Docker Desktop (o Docker Engine en Linux) | 24 o más reciente | Construir las imágenes y correr las pruebas. No hace falta gcc, Java, pandoc ni LaTeX: todo se compila dentro de contenedores |
| Terraform | 1.11 o más reciente | Crear la infraestructura en AWS |
| AWS CLI | 2 | Credenciales de AWS, el token de ECR y traer el `kubeconfig` |
| kubectl | 1.29 o más reciente | Hablar con Kubernetes |
| Helm | 3 | Instalar el Helm Chart |
| just | 1.20 o más reciente | Correr las recetas del `justfile` |

**macOS**, con [Homebrew](https://brew.sh):

```bash
brew install git just kubectl helm awscli hashicorp/tap/terraform
brew install --cask docker
```

**Windows**, desde PowerShell con `winget`. Las recetas del `justfile` son de bash, así que se corren desde **Git Bash**, que viene con Git:

```powershell
winget install Git.Git Docker.DockerDesktop Hashicorp.Terraform Amazon.AWSCLI Kubernetes.kubectl Helm.Helm Casey.Just
```

**Linux** (Debian o Ubuntu): Docker Engine según [docs.docker.com/engine/install](https://docs.docker.com/engine/install/), Terraform según [developer.hashicorp.com/terraform/install](https://developer.hashicorp.com/terraform/install), la AWS CLI según [docs.aws.amazon.com/cli](https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html), y `kubectl`, `helm` y `just` con el gestor de paquetes o sus binarios oficiales.

Abra Docker Desktop y espere a que diga que está corriendo. Revise que todo quedó instalado:

```bash
git --version
docker info --format '{{.ServerVersion}}'
terraform version
aws --version
kubectl version --client
helm version
just --version
```

Si un comando dice `command not found`, cierre y vuelva a abrir la terminal; si sigue, ese programa no quedó instalado.

## 2. Clonar el repositorio y crear `.env`

```bash
git clone https://github.com/JoseZum/2026-02-2024080534-IC7602-P2.git
cd 2026-02-2024080534-IC7602-P2
cp .env.example .env
```

`.env` guarda las credenciales y direcciones de su instalación. Está en el `.gitignore`: nunca se sube al repositorio ni va en el `tar.gz` de la entrega. El `justfile` lo carga solo en cada receta.

Abra `.env` con cualquier editor. Estas son las variables que usa la instalación; las demás son para correr los componentes en local.

| Variable | Ejemplo | Qué es y de dónde sale |
| --- | --- | --- |
| `SUPABASE_DB_URL` | `postgresql://postgres.abc:clave@aws-0-us-east-1.pooler.supabase.com:5432/postgres` | Conexión a la base de Supabase (sección 4). La usan `just db` y `just up` para dejar las Elastic IP en la tabla `zones` |
| `NODE_API_URL` | `https://zonal-api.duran-munozalonso.workers.dev` | URL del REST API Node.js en Cloudflare (sección 5). **No** puede ser `http://127.0.0.1:8787`: la EC2 no llega a su computadora |
| `LLAVE_INTERCEPTOR` | `dev-interceptor-key` | Llave de componente con alcance `interceptor`. Las de desarrollo están en `supabase/seed.sql` |
| `LLAVE_CACHE` | `dev-cache-key` | Llave de componente con alcance `cache` |
| `UI_LOGIN_URL` | `https://<ui>.pages.dev/login` | Formulario de login de la UI, a donde la caché manda las URLs con usuario y contraseña |
| `TF_VAR_ips_equipo` | `'["201.191.10.20/32"]'` | IP pública de cada integrante con `/32` (sección 3), entre comillas simples. Solo esas IP llegan al API de Kubernetes |
| `TF_VAR_perfil_instancia` | `LabInstanceProfile` | Solo en AWS Academy (sección 3) |
| `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, `AWS_SESSION_TOKEN` | | Credenciales de AWS, si no se usa `aws configure` (sección 3) |

Otras variables de Terraform se pueden cambiar con el prefijo `TF_VAR_`: la región (`TF_VAR_region`, por defecto `us-east-1`), las zonas (`TF_VAR_zonas`, por defecto `["CR","US","ES","GB"]`) o el tipo de EC2 (`TF_VAR_tipo_instancia`). La lista completa está en [infraestructura.md](../03-arquitectura/infraestructura.md#variables).

## 3. Credenciales de AWS

### Con una cuenta normal de AWS

Cree en IAM un usuario con una llave de acceso (IAM → Users → Create user → Security credentials → Create access key) y configúrela:

```bash
aws configure
# AWS Access Key ID: AKIA...
# AWS Secret Access Key: ...
# Default region name: us-east-1
# Default output format: json
```

El usuario necesita permisos para EC2, VPC, ECR, SSM Parameter Store e IAM (para crear el rol de la EC2). Con `AdministratorAccess` alcanza.

### Con AWS Academy (Learner Lab)

1. Inicie el lab con **Start Lab** y espere a que el círculo quede verde.
2. En **AWS Details → AWS CLI → Show**, copie las tres líneas a `.env` como `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY` y `AWS_SESSION_TOKEN`, sin espacios alrededor del `=`.
3. En `.env`, descomente `TF_VAR_perfil_instancia=LabInstanceProfile`. El lab no deja crear roles de IAM y trae ese perfil ya hecho.
4. Use la región `us-east-1`, que es la que deja el lab.

Las credenciales del lab cambian cada vez que se inicia. Si un comando dice `ExpiredToken`, vuelva a copiarlas.

### Revisar las credenciales y su IP pública

```bash
set -a; . ./.env; set +a     # carga .env en esta terminal
aws sts get-caller-identity
curl https://checkip.amazonaws.com
```

El primer comando tiene que mostrar su `Account`. El segundo da su IP pública: póngala en `.env` con `/32`. Si son varios integrantes, van separadas por coma:

```bash
TF_VAR_ips_equipo='["201.191.10.20/32","186.15.40.2/32"]'
```

Las comillas simples hacen falta: sin ellas, `.env` pierde las comillas dobles y Terraform no lo lee como una lista.

## 4. Supabase

1. Cree un proyecto gratuito en [supabase.com](https://supabase.com) y guarde la contraseña de la base.
2. En **Connect → Connection string → Session pooler**, copie la cadena, cambie `[YOUR-PASSWORD]` por la contraseña y póngala en `.env` como `SUPABASE_DB_URL`.
3. Aplique el esquema, los datos de prueba y la tabla IP-to-Country:

```bash
just db
just db-test
```

`just db` se puede repetir sin duplicar nada. `just db-test` corre las pruebas de RLS y termina sin errores. Los datos de prueba cargan los cuatro dominios de los orígenes ya verificados, así que no hace falta la UI para probar. El detalle está en [supabase.md](../03-arquitectura/supabase.md).

## 5. Cloudflare

El REST API Node.js ya está desplegado en `https://zonal-api.duran-munozalonso.workers.dev` y se vuelve a desplegar solo con cada merge a `main`. Si va a usar ese, deje `NODE_API_URL` con esa URL y no hay nada más que hacer. Revise que responde:

```bash
curl https://zonal-api.duran-munozalonso.workers.dev/health
curl -H "x-api-key: $LLAVE_CACHE" https://zonal-api.duran-munozalonso.workers.dev/api/cache/config
```

El primero da `200` con `{"status":"ok Im alive and well"}` y el segundo la lista de URLs. Si da `401`, la llave de `LLAVE_CACHE` no está en la base de ese Worker.

Para montar el Worker en otra cuenta de Cloudflare, con su propio Supabase, siga [node-api.md](node-api.md) y ponga la URL nueva en `NODE_API_URL`.

## 6. Crear la infraestructura: `just infra`

```bash
just infra
```

Terraform crea la VPC, el security group, la EC2, las Elastic IP, los repositorios de ECR y el parámetro de SSM. La EC2 instala k3s sola al arrancar y publica su `kubeconfig`; `just infra` espera a que esté listo y lo deja en `terraform/kubeconfig`. Tarda unos 5 minutos. Al final muestra algo así:

```
zonas = {
  "CR" = { "ip_privada" = "10.20.1.11", "ip_publica" = "3.214.10.5" }
  "ES" = { "ip_privada" = "10.20.1.13", "ip_publica" = "44.196.2.17" }
  ...
}
Interceptor (nslookup): 54.80.12.34
kubeconfig: .../terraform/kubeconfig
Siga con: just up
```

Revise que llega al clúster:

```bash
export KUBECONFIG=$PWD/terraform/kubeconfig
kubectl get nodes
```

Tiene que salir un nodo `Ready`. Cómo funciona por dentro está en [infraestructura.md](../03-arquitectura/infraestructura.md).

## 7. Instalar los componentes: `just up`

```bash
just up
```

`just up` hace todo en orden:

1. Crea la CA local en `.certs/` si no existe (`just certs`).
2. Construye las cuatro imágenes para la arquitectura de la EC2 y las publica en ECR. Cada imagen corre sus pruebas al construirse: si una falla, se detiene aquí.
3. Crea el namespace `p2` y el `Secret` para bajar de ECR.
4. Instala o actualiza el Helm Chart con las IP que dio Terraform, las llaves y la CA, y espera a que todos los pods estén listos.
5. Deja las Elastic IP de cada zona en la tabla `zones` de Supabase (`just zonas`), para que el Interceptor responda las IP reales.

La primera vez tarda más, porque baja las imágenes base; en una Mac con Apple Silicon y una EC2 `t3` las imágenes se construyen emuladas y puede tardar unos 10 minutos. Al final muestra los pods y los `Service`:

```
NAME                               READY   STATUS    ...
pod/apache-6d9...                  1/1     Running
pod/cache-cr-7f8...                1/1     Running
pod/cache-es-...                   1/1     Running
pod/cache-gb-...                   1/1     Running
pod/cache-us-...                   1/1     Running
pod/interceptor-5c4...             1/1     Running
pod/java-api-84b...                1/1     Running
```

`just up` se puede repetir cada vez que cambia el código: cada commit es una etiqueta de imagen nueva y Helm reinicia solo lo que cambió. El chart está documentado en [helm.md](../03-arquitectura/helm.md).

## 8. Probar que funciona

Con la IP del Interceptor que mostró `just infra` (también sale con `terraform -chdir=terraform output -raw ip_interceptor`):

```bash
nslookup java-api.test 54.80.12.34
```

Responde la Elastic IP de la caché de la zona de su país. Con esa IP:

```bash
curl -i --resolve java-api.test:80:3.214.10.5 http://java-api.test/api/productos
curl -i --resolve java-api.test:80:3.214.10.5 http://java-api.test/api/productos
```

La segunda respuesta trae `X-Cache: HIT`: salió del disco de la caché sin ir al API Java. La prueba completa, con los cuatro orígenes, las dos autenticaciones y el límite del disco, se corre con:

```bash
just integracion
```

Cómo reproducir cada prueba a mano está en [docs/05-pruebas](../05-pruebas/README.md).

Para ver qué está pasando en un componente:

```bash
just logs cache-cr
just logs interceptor
just logs java-api
```

## 9. Apagar todo

```bash
just down          # desinstala el Helm Chart y deja la EC2
just infra-down    # destruye todo lo de AWS: EC2, Elastic IP, ECR y el kubeconfig
```

Después de `just infra-down` no queda nada cobrando en AWS. Para volver a levantarlo basta con `just infra` y `just up`; las Elastic IP van a ser otras y `just up` las vuelve a dejar en Supabase.

## 10. Solución de problemas

| Síntoma | Causa | Qué hacer |
| --- | --- | --- |
| `Falta TF_VAR_ips_equipo en .env` | No se copió `.env.example` o falta la variable | Sección 2 y 3 |
| `Invalid number literal` en `<value for var.ips_equipo>` | Faltan las comillas simples alrededor de la lista en `.env` | `TF_VAR_ips_equipo='["x.x.x.x/32"]'` |
| `ExpiredToken` o `InvalidClientTokenId` | Las credenciales de AWS Academy cambiaron | Vuelva a copiarlas de AWS Details a `.env` |
| `AddressLimitExceeded` al crear las Elastic IP | AWS da 5 Elastic IP por región y ya hay otras en la cuenta | Libere las que no use en EC2 → Elastic IPs, o baje las zonas con `TF_VAR_zonas=["CR","US"]` |
| `UnauthorizedOperation` al crear el rol `ic7602-p2-nodo` | AWS Academy no deja crear roles | `TF_VAR_perfil_instancia=LabInstanceProfile` en `.env` |
| `La EC2 no publicó el kubeconfig` | k3s no terminó de instalar en 10 minutos | En la consola de AWS, EC2 → la instancia `ic7602-p2` → Actions → Monitor → Get system log, y busque la última línea de `/var/log/p2-arranque.log`. Casi siempre es falta de salida a Internet; vuelva a correr `just infra` |
| `kubectl` dice `i/o timeout` o `connection refused` en el 6443 | Su IP pública cambió y ya no está en `TF_VAR_ips_equipo` | `curl https://checkip.amazonaws.com`, actualice `.env` y corra `just infra`; solo cambia el security group |
| `No existe terraform/kubeconfig; corra primero just infra` | `just up` antes de `just infra`, o después de `just infra-down` | `just infra` |
| Un pod en `ImagePullBackOff` | El token de ECR venció (dura 12 horas) | `just up` lo renueva |
| Un pod de caché o el Interceptor en `CrashLoopBackOff` | Falta una variable o no llega al REST API | `just logs cache-cr`: la caché dice qué variable falta. Si dice que no pudo descargar la configuración, revise `NODE_API_URL` y `LLAVE_CACHE` (sección 5) |
| `docker build src/zonal-cache` falla en `make test` | Una prueba de Criterion falla | La imagen no se construye con pruebas rotas a propósito; el error dice cuál falló |
| `nslookup` no responde | Su red bloquea DNS hacia afuera, o `just up` no terminó | Pruebe desde otra red (por ejemplo el celular) o con `nslookup -port=53`; revise `just logs interceptor` |
| `nslookup` responde `SERVFAIL` para un dominio nuestro | El país de su IP no tiene zona en la tabla `zones` | Agregue el país al arreglo `countries` de una zona en Supabase |
| `curl` responde la IP de documentación `203.0.113.x` | La tabla `zones` todavía tiene las IP de los datos de prueba | `just zonas` (necesita `SUPABASE_DB_URL`) |
| `just certs` dice `La CA local está incompleta` | Falta uno de los dos archivos en `.certs/` | Borre `.certs/` y corra `just certs` de nuevo; después `just up` |
| `just docs` tarda mucho en una Mac con Apple Silicon | La imagen de pandoc solo existe para amd64 y corre emulada | Es normal la primera vez; las siguientes usan la imagen ya construida |
| En Windows, `just` dice que no encuentra `bash` | Se corrió desde PowerShell o CMD | Use Git Bash |
