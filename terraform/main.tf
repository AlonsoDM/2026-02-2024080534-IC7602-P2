locals {
  cidr_subred = cidrsubnet(var.cidr_vpc, 8, 1)

  # La IP privada principal es del Interceptor (53/udp) y del API de Kubernetes; cada zona
  # recibe una secundaria fija, así Helm sabe de antemano cuál escucha cada caché
  ip_privada_principal = cidrhost(local.cidr_subred, 10)
  ips_privadas_zonas   = { for i, zona in var.zonas : zona => cidrhost(local.cidr_subred, 11 + i) }

  parametro_kubeconfig = "/${var.proyecto}/kubeconfig"
  ruta_kubeconfig      = var.ruta_kubeconfig != "" ? var.ruta_kubeconfig : "${path.module}/kubeconfig"

  imagenes = ["interceptor", "zonal-cache", "java-api", "apache"]

  # Sin servicios web abiertos a nadie más: DNS, HTTP y HTTPS para todos, y el API de Kubernetes solo al equipo
  puertos_publicos = {
    dns   = { protocolo = "udp", puerto = 53 }
    http  = { protocolo = "tcp", puerto = 80 }
    https = { protocolo = "tcp", puerto = 443 }
  }
}

data "aws_availability_zones" "disponibles" {
  state = "available"
}

# AMI de Amazon Linux 2023: trae la AWS CLI y amazon-ec2-net-utils, que configura solas las IP secundarias
data "aws_ssm_parameter" "ami" {
  name = "/aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-${var.arquitectura}"
}

# Red
resource "aws_vpc" "principal" {
  cidr_block           = var.cidr_vpc
  enable_dns_support   = true
  enable_dns_hostnames = true

  tags = { Name = var.proyecto }
}

resource "aws_internet_gateway" "principal" {
  vpc_id = aws_vpc.principal.id

  tags = { Name = var.proyecto }
}

resource "aws_subnet" "publica" {
  vpc_id            = aws_vpc.principal.id
  cidr_block        = local.cidr_subred
  availability_zone = data.aws_availability_zones.disponibles.names[0]

  tags = { Name = "${var.proyecto}-publica" }
}

resource "aws_route_table" "publica" {
  vpc_id = aws_vpc.principal.id

  route {
    cidr_block = "0.0.0.0/0"
    gateway_id = aws_internet_gateway.principal.id
  }

  tags = { Name = "${var.proyecto}-publica" }
}

resource "aws_route_table_association" "publica" {
  subnet_id      = aws_subnet.publica.id
  route_table_id = aws_route_table.publica.id
}

# Security group
resource "aws_security_group" "nodo" {
  name        = "${var.proyecto}-nodo"
  description = "DNS, HTTP y HTTPS publicos; API de Kubernetes solo desde las IP del equipo"
  vpc_id      = aws_vpc.principal.id

  tags = { Name = "${var.proyecto}-nodo" }
}

resource "aws_vpc_security_group_ingress_rule" "publicos" {
  for_each = local.puertos_publicos

  security_group_id = aws_security_group.nodo.id
  description       = each.key
  ip_protocol       = each.value.protocolo
  from_port         = each.value.puerto
  to_port           = each.value.puerto
  cidr_ipv4         = "0.0.0.0/0"
}

resource "aws_vpc_security_group_ingress_rule" "kubernetes" {
  for_each = toset(var.ips_equipo)

  security_group_id = aws_security_group.nodo.id
  description       = "API de Kubernetes"
  ip_protocol       = "tcp"
  from_port         = 6443
  to_port           = 6443
  cidr_ipv4         = each.value
}

# La EC2 sale a Internet para bajar k3s, las imágenes de ECR, los sitios externos y el REST API Node.js
resource "aws_vpc_security_group_egress_rule" "todo" {
  security_group_id = aws_security_group.nodo.id
  description       = "Salida"
  ip_protocol       = "-1"
  cidr_ipv4         = "0.0.0.0/0"
}

# Elastic IP. Se crean sin asociar para que su dirección exista antes que la EC2 y pueda ir en el user_data
resource "aws_eip" "interceptor" {
  domain = "vpc"

  tags = { Name = "${var.proyecto}-interceptor" }
}

resource "aws_eip" "zona" {
  for_each = local.ips_privadas_zonas

  domain = "vpc"

  tags = { Name = "${var.proyecto}-cache-${each.key}" }
}

# Parámetro donde la EC2 deja el kubeconfig. Terraform lo crea vacío para poder borrarlo con infra-down
resource "aws_ssm_parameter" "kubeconfig" {
  name        = local.parametro_kubeconfig
  description = "kubeconfig de k3s, lo escribe la EC2 al terminar de instalar"
  type        = "SecureString"
  tier        = "Intelligent-Tiering"
  value       = "pendiente"

  lifecycle {
    ignore_changes = [value]
  }
}

# Repositorios de ECR para las cuatro imágenes. force_delete deja que infra-down los borre con imágenes adentro
resource "aws_ecr_repository" "imagen" {
  for_each = toset(local.imagenes)

  name                 = "${var.proyecto}/${each.key}"
  image_tag_mutability = "MUTABLE"
  force_delete         = true
}

resource "aws_instance" "nodo" {
  ami                    = data.aws_ssm_parameter.ami.value
  instance_type          = var.tipo_instancia
  subnet_id              = aws_subnet.publica.id
  vpc_security_group_ids = [aws_security_group.nodo.id]
  iam_instance_profile   = local.perfil_instancia

  private_ip            = local.ip_privada_principal
  secondary_private_ips = values(local.ips_privadas_zonas)

  # Sin IP pública automática: el nodo sale por la Elastic IP del Interceptor
  associate_public_ip_address = false

  user_data = templatefile("${path.module}/arranque.sh.tftpl", {
    version_k3s     = var.version_k3s
    ip_publica      = aws_eip.interceptor.public_ip
    ips_secundarias = join(" ", values(local.ips_privadas_zonas))
    prefijo         = split("/", local.cidr_subred)[1]
    region          = var.region
    parametro       = aws_ssm_parameter.kubeconfig.name
    contexto        = var.proyecto
  })
  user_data_replace_on_change = true

  root_block_device {
    volume_size = var.disco_gb
    volume_type = "gp3"
  }

  metadata_options {
    http_tokens = "required"
  }

  tags = { Name = var.proyecto }

  depends_on = [aws_internet_gateway.principal]
}

resource "aws_eip_association" "interceptor" {
  allocation_id        = aws_eip.interceptor.id
  network_interface_id = aws_instance.nodo.primary_network_interface_id
  private_ip_address   = local.ip_privada_principal
}

resource "aws_eip_association" "zona" {
  for_each = local.ips_privadas_zonas

  allocation_id        = aws_eip.zona[each.key].id
  network_interface_id = aws_instance.nodo.primary_network_interface_id
  private_ip_address   = each.value
}

# Espera a que la EC2 publique el kubeconfig y lo deja en la compu que corre Terraform
resource "terraform_data" "kubeconfig" {
  triggers_replace = [aws_instance.nodo.id]

  provisioner "local-exec" {
    interpreter = ["sh", "-c"]
    command     = "sh '${path.module}/obtener-kubeconfig.sh'"

    environment = {
      REGION          = var.region
      PARAMETRO       = aws_ssm_parameter.kubeconfig.name
      RUTA_KUBECONFIG = local.ruta_kubeconfig
    }
  }

  depends_on = [aws_eip_association.interceptor, aws_eip_association.zona]
}
