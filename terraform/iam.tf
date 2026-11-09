# La EC2 solo necesita escribir el parámetro del kubeconfig. En AWS Academy no se pueden crear
# roles, así que ahí se pasa perfil_instancia = "LabInstanceProfile" y no se crea nada de esto
locals {
  crear_perfil     = var.perfil_instancia == ""
  perfil_instancia = local.crear_perfil ? aws_iam_instance_profile.nodo[0].name : var.perfil_instancia
}

data "aws_iam_policy_document" "asumir_ec2" {
  statement {
    actions = ["sts:AssumeRole"]

    principals {
      type        = "Service"
      identifiers = ["ec2.amazonaws.com"]
    }
  }
}

data "aws_iam_policy_document" "kubeconfig" {
  statement {
    actions   = ["ssm:PutParameter"]
    resources = [aws_ssm_parameter.kubeconfig.arn]
  }
}

resource "aws_iam_role" "nodo" {
  count = local.crear_perfil ? 1 : 0

  name               = "${var.proyecto}-nodo"
  assume_role_policy = data.aws_iam_policy_document.asumir_ec2.json
}

resource "aws_iam_role_policy" "kubeconfig" {
  count = local.crear_perfil ? 1 : 0

  name   = "escribir-kubeconfig"
  role   = aws_iam_role.nodo[0].id
  policy = data.aws_iam_policy_document.kubeconfig.json
}

resource "aws_iam_instance_profile" "nodo" {
  count = local.crear_perfil ? 1 : 0

  name = "${var.proyecto}-nodo"
  role = aws_iam_role.nodo[0].name
}
