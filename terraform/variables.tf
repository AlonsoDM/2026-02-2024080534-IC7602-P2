variable "region" {
  description = "Región de AWS donde se crea todo. AWS Academy solo deja us-east-1 y us-west-2"
  type        = string
  default     = "us-east-1"
}

variable "proyecto" {
  description = "Prefijo de los nombres de los recursos, del parámetro del kubeconfig y de los repositorios de ECR"
  type        = string
  default     = "ic7602-p2"

  validation {
    condition     = can(regex("^[a-z][a-z0-9-]{1,30}$", var.proyecto))
    error_message = "proyecto solo puede tener minúsculas, dígitos y guiones, y empezar con una letra."
  }
}

variable "zonas" {
  description = "Código de cada caché zonal. Cada una recibe una IP privada secundaria con su Elastic IP"
  type        = list(string)
  default     = ["CR", "US", "ES", "GB"]

  validation {
    condition     = length(var.zonas) > 0 && length(distinct(var.zonas)) == length(var.zonas)
    error_message = "Tiene que haber al menos una zona y no se pueden repetir."
  }

  # AWS da 5 Elastic IP por región y una es del Interceptor
  validation {
    condition     = length(var.zonas) <= 4
    error_message = "Caben a lo sumo 4 zonas: AWS da 5 Elastic IP por región y una es del Interceptor."
  }

  validation {
    condition     = alltrue([for z in var.zonas : can(regex("^[A-Z]{2}$", z))])
    error_message = "Cada zona es un código de país de dos letras en mayúscula, por ejemplo CR."
  }
}

variable "ips_equipo" {
  description = "Rangos CIDR desde los que se abre el API de Kubernetes (6443), por ejemplo [\"201.191.10.20/32\"]"
  type        = list(string)

  validation {
    condition     = length(var.ips_equipo) > 0 && alltrue([for c in var.ips_equipo : can(cidrhost(c, 0))])
    error_message = "ips_equipo debe tener al menos un rango CIDR válido, por ejemplo 201.191.10.20/32."
  }

  validation {
    condition     = !contains(var.ips_equipo, "0.0.0.0/0")
    error_message = "El API de Kubernetes no se abre a todo Internet: use la IP pública de cada integrante con /32."
  }
}

variable "tipo_instancia" {
  description = "Tipo de la EC2. Con 4 GB caben k3s, el Interceptor, las cachés y los dos orígenes"
  type        = string
  default     = "t3.medium"
}

variable "arquitectura" {
  description = "Arquitectura de la AMI y de las imágenes: x86_64 para t3, arm64 para t4g"
  type        = string
  default     = "x86_64"

  validation {
    condition     = contains(["x86_64", "arm64"], var.arquitectura)
    error_message = "arquitectura debe ser x86_64 o arm64."
  }
}

variable "disco_gb" {
  description = "Tamaño del disco raíz en GB"
  type        = number
  default     = 20
}

variable "version_k3s" {
  description = "Versión de k3s que se instala"
  type        = string
  default     = "v1.31.4+k3s1"
}

variable "cidr_vpc" {
  description = "Rango de la VPC. La subred pública es el primer /24"
  type        = string
  default     = "10.20.0.0/16"
}

variable "perfil_instancia" {
  description = "Instance profile ya existente para la EC2 (en AWS Academy, LabInstanceProfile). Vacío crea uno propio"
  type        = string
  default     = ""
}

variable "ruta_kubeconfig" {
  description = "Dónde se escribe el kubeconfig del clúster en la compu que corre Terraform"
  type        = string
  default     = ""
}
