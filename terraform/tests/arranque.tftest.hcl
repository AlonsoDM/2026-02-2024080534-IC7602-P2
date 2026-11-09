# Revisa el user_data ya armado, sin tocar AWS.

run "user_data_instala_k3s_y_publica_el_kubeconfig" {
  command = apply

  module {
    source = "./tests/plantilla"
  }

  variables {
    valores = {
      version_k3s     = "v1.31.4+k3s1"
      ip_publica      = "198.51.100.10"
      ips_secundarias = "10.20.1.11 10.20.1.12"
      prefijo         = "24"
      region          = "us-east-1"
      parametro       = "/ic7602-p2/kubeconfig"
      contexto        = "ic7602-p2"
    }
  }

  assert {
    condition     = strcontains(output.user_data, "--disable traefik --disable servicelb")
    error_message = "Traefik y ServiceLB se desactivan para dejar 53, 80 y 443 a los Service con externalIPs"
  }

  assert {
    condition     = strcontains(output.user_data, "--tls-san 198.51.100.10")
    error_message = "El certificado del API debe incluir la Elastic IP"
  }

  assert {
    condition     = strcontains(output.user_data, "INSTALL_K3S_VERSION='v1.31.4+k3s1'")
    error_message = "La versión de k3s va fija"
  }

  assert {
    condition     = strcontains(output.user_data, "for ip in 10.20.1.11 10.20.1.12; do")
    error_message = "Se revisan las IP secundarias de cada zona"
  }

  assert {
    condition     = strcontains(output.user_data, "https://198.51.100.10:6443")
    error_message = "El kubeconfig publicado apunta a la Elastic IP"
  }

  assert {
    condition     = strcontains(output.user_data, "--name '/ic7602-p2/kubeconfig' --type SecureString")
    error_message = "El kubeconfig se publica cifrado en el parámetro de SSM"
  }
}
