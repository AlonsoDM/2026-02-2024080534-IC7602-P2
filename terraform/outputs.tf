output "ip_interceptor" {
  description = "Elastic IP del Interceptor (53/udp) y del API de Kubernetes. Es la que se usa en nslookup"
  value       = aws_eip.interceptor.public_ip
}

output "ip_privada_interceptor" {
  description = "IP privada principal del nodo; el Service del Interceptor la escucha con externalIPs"
  value       = local.ip_privada_principal
}

output "zonas" {
  description = "Por zona, la Elastic IP que va en la tabla zones de Supabase y la IP privada que va en externalIPs de Helm"
  value = {
    for zona, privada in local.ips_privadas_zonas : zona => {
      ip_publica = aws_eip.zona[zona].public_ip
      ip_privada = privada
    }
  }
}

output "registro" {
  description = "Registro de ECR donde just up publica las imágenes"
  value       = split("/", aws_ecr_repository.imagen["zonal-cache"].repository_url)[0]
}

output "repositorios" {
  description = "URL de cada repositorio de ECR"
  value       = { for nombre, repo in aws_ecr_repository.imagen : nombre => repo.repository_url }
}

output "plataforma" {
  description = "Plataforma con la que se construyen las imágenes para este nodo"
  value       = var.arquitectura == "arm64" ? "linux/arm64" : "linux/amd64"
}

output "region" {
  value = var.region
}

output "kubeconfig" {
  description = "Ruta del kubeconfig listo para kubectl y helm"
  value       = abspath(local.ruta_kubeconfig)
}

output "valores_helm" {
  description = "Valores de Helm que salen de la infraestructura: registro de las imágenes, IP del Interceptor y de cada caché"
  value = yamlencode({
    imagenes = {
      registro = "${split("/", aws_ecr_repository.imagen["zonal-cache"].repository_url)[0]}/${var.proyecto}"
    }
    interceptor = {
      externalIPs = [local.ip_privada_principal]
    }
    cache = {
      zonas = [for zona in var.zonas : { codigo = zona, externalIP = local.ips_privadas_zonas[zona] }]
    }
  })
}

output "sql_zonas" {
  description = "SQL que deja en la tabla zones de Supabase la Elastic IP de cada caché"
  value = join("\n", [
    for zona in var.zonas : "update zones set ip = '${aws_eip.zona[zona].public_ip}' where code = '${zona}';"
  ])
}
