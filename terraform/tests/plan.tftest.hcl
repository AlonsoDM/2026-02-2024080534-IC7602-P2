# Pruebas del plan con un proveedor de AWS simulado: no hacen falta credenciales ni se crea nada.
#   terraform init && terraform test

mock_provider "aws" {
  override_data {
    target = data.aws_availability_zones.disponibles
    values = { names = ["us-east-1a", "us-east-1b"] }
  }

  override_data {
    target = data.aws_ssm_parameter.ami
    values = { value = "ami-0123456789abcdef0" }
  }

  override_data {
    target = data.aws_iam_policy_document.asumir_ec2
    values = { json = "{}" }
  }

  override_data {
    target = data.aws_iam_policy_document.kubeconfig
    values = { json = "{}" }
  }

  override_resource {
    target          = aws_eip.interceptor
    override_during = plan
    values          = { public_ip = "198.51.100.10", id = "eipalloc-interceptor" }
  }
}

variables {
  ips_equipo = ["201.191.10.20/32", "186.15.0.0/24"]
}

run "una_ip_secundaria_y_una_elastic_ip_por_zona" {
  command = plan

  assert {
    condition     = length(aws_eip.zona) == 4 && length(aws_eip_association.zona) == 4
    error_message = "Cada una de las 4 zonas por defecto debe tener su Elastic IP asociada"
  }

  assert {
    condition     = aws_instance.nodo.private_ip == "10.20.1.10"
    error_message = "La IP privada principal debe ser la 10 de la subred"
  }

  assert {
    condition     = toset(aws_instance.nodo.secondary_private_ips) == toset(["10.20.1.11", "10.20.1.12", "10.20.1.13", "10.20.1.14"])
    error_message = "Cada zona debe tener su IP privada secundaria fija"
  }

  assert {
    condition     = aws_eip_association.zona["CR"].private_ip_address == "10.20.1.11" && aws_eip_association.zona["GB"].private_ip_address == "10.20.1.14"
    error_message = "La Elastic IP de cada zona debe ir a la IP secundaria de esa zona"
  }

  assert {
    condition     = output.zonas["ES"].ip_privada == "10.20.1.13"
    error_message = "La salida zonas debe dar la IP privada de cada zona para Helm"
  }

  assert {
    condition     = aws_instance.nodo.associate_public_ip_address == false
    error_message = "La EC2 no debe tener IP pública automática, sale por las Elastic IP"
  }
}

run "security_group_solo_abre_lo_que_se_usa" {
  command = plan

  assert {
    condition     = toset(keys(aws_vpc_security_group_ingress_rule.publicos)) == toset(["dns", "http", "https"])
    error_message = "Solo DNS, HTTP y HTTPS se abren a todo Internet"
  }

  assert {
    condition     = aws_vpc_security_group_ingress_rule.publicos["dns"].ip_protocol == "udp" && aws_vpc_security_group_ingress_rule.publicos["dns"].from_port == 53
    error_message = "El DNS se abre en 53/udp"
  }

  assert {
    condition     = aws_vpc_security_group_ingress_rule.publicos["http"].from_port == 80 && aws_vpc_security_group_ingress_rule.publicos["https"].from_port == 443
    error_message = "HTTP en 80 y HTTPS en 443"
  }

  assert {
    condition     = alltrue([for r in aws_vpc_security_group_ingress_rule.publicos : r.cidr_ipv4 == "0.0.0.0/0"])
    error_message = "DNS, HTTP y HTTPS se abren para cualquier cliente"
  }

  assert {
    condition     = toset([for r in aws_vpc_security_group_ingress_rule.kubernetes : r.cidr_ipv4]) == toset(["201.191.10.20/32", "186.15.0.0/24"])
    error_message = "El API de Kubernetes solo se abre a las IP del equipo"
  }

  assert {
    condition     = alltrue([for r in aws_vpc_security_group_ingress_rule.kubernetes : r.from_port == 6443 && r.to_port == 6443 && r.ip_protocol == "tcp"])
    error_message = "El API de Kubernetes es 6443/tcp"
  }
}

run "ecr_y_kubeconfig" {
  command = plan

  assert {
    condition     = toset([for r in aws_ecr_repository.imagen : r.name]) == toset(["ic7602-p2/interceptor", "ic7602-p2/zonal-cache", "ic7602-p2/java-api", "ic7602-p2/apache"])
    error_message = "Debe haber un repositorio de ECR por imagen"
  }

  assert {
    condition     = alltrue([for r in aws_ecr_repository.imagen : r.force_delete])
    error_message = "infra-down debe poder borrar los repositorios aunque tengan imágenes"
  }

  assert {
    condition     = aws_ssm_parameter.kubeconfig.name == "/ic7602-p2/kubeconfig" && aws_ssm_parameter.kubeconfig.type == "SecureString"
    error_message = "El kubeconfig se guarda cifrado en /ic7602-p2/kubeconfig"
  }

  assert {
    condition     = length(aws_iam_instance_profile.nodo) == 1
    error_message = "Sin perfil_instancia se crea un instance profile propio"
  }
}

run "aws_academy_usa_el_perfil_existente" {
  command = plan

  variables {
    perfil_instancia = "LabInstanceProfile"
  }

  assert {
    condition     = length(aws_iam_role.nodo) == 0 && length(aws_iam_instance_profile.nodo) == 0
    error_message = "Con perfil_instancia no se crean roles, AWS Academy no lo permite"
  }

  assert {
    condition     = aws_instance.nodo.iam_instance_profile == "LabInstanceProfile"
    error_message = "La EC2 debe usar el perfil que se pasó"
  }
}

run "menos_zonas_y_arm64" {
  command = plan

  variables {
    zonas          = ["CR", "US"]
    arquitectura   = "arm64"
    tipo_instancia = "t4g.medium"
  }

  assert {
    condition     = length(aws_eip.zona) == 2 && toset(aws_instance.nodo.secondary_private_ips) == toset(["10.20.1.11", "10.20.1.12"])
    error_message = "Con 2 zonas solo se crean 2 IP secundarias y 2 Elastic IP"
  }

  assert {
    condition     = output.plataforma == "linux/arm64"
    error_message = "Las imágenes se construyen para arm64"
  }
}

run "rechaza_el_api_abierto_a_todos" {
  command = plan

  variables {
    ips_equipo = ["0.0.0.0/0"]
  }

  expect_failures = [var.ips_equipo]
}

run "rechaza_un_cidr_invalido" {
  command = plan

  variables {
    ips_equipo = ["201.191.10.20"]
  }

  expect_failures = [var.ips_equipo]
}

run "rechaza_mas_de_cuatro_zonas" {
  command = plan

  variables {
    zonas = ["CR", "US", "ES", "GB", "MX"]
  }

  expect_failures = [var.zonas]
}

run "rechaza_zonas_repetidas_o_mal_escritas" {
  command = plan

  variables {
    zonas = ["CR", "cr"]
  }

  expect_failures = [var.zonas]
}

run "salidas_para_helm_y_supabase" {
  command = plan

  variables {
    zonas = ["CR", "US"]
  }

  override_resource {
    target          = aws_eip.zona
    override_during = plan
    values          = { public_ip = "203.0.113.50" }
  }

  override_resource {
    target          = aws_ecr_repository.imagen
    override_during = plan
    values          = { repository_url = "123456789012.dkr.ecr.us-east-1.amazonaws.com/ic7602-p2/x" }
  }

  assert {
    condition = yamldecode(output.valores_helm) == {
      imagenes    = { registro = "123456789012.dkr.ecr.us-east-1.amazonaws.com/ic7602-p2" }
      interceptor = { externalIPs = ["10.20.1.10"] }
      cache = { zonas = [
        { codigo = "CR", externalIP = "10.20.1.11" },
        { codigo = "US", externalIP = "10.20.1.12" },
      ] }
    }
    error_message = "valores_helm debe traer el registro, la IP del Interceptor y la IP privada de cada zona"
  }

  assert {
    condition     = output.sql_zonas == "update zones set ip = '203.0.113.50' where code = 'CR';\nupdate zones set ip = '203.0.113.50' where code = 'US';"
    error_message = "sql_zonas debe actualizar la IP pública de cada zona"
  }
}
