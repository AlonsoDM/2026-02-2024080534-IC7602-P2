# Módulo auxiliar de las pruebas: arma el user_data con valores de ejemplo para revisar su contenido
variable "valores" {
  type = map(string)
}

output "user_data" {
  value = templatefile("${path.module}/../../arranque.sh.tftpl", var.valores)
}
