{{/* Etiquetas comunes de todos los objetos */}}
{{- define "proxy-cache.etiquetas" -}}
app.kubernetes.io/part-of: {{ .Chart.Name }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{/* Imagen completa: registro/nombre:etiqueta. Recibe (dict "raiz" $ "nombre" "zonal-cache") */}}
{{- define "proxy-cache.imagen" -}}
{{- $img := .raiz.Values.imagenes -}}
{{- if $img.registro -}}
{{- printf "%s/%s:%s" (trimSuffix "/" $img.registro) .nombre $img.etiqueta -}}
{{- else -}}
{{- printf "%s:%s" .nombre $img.etiqueta -}}
{{- end -}}
{{- end -}}

{{- define "proxy-cache.pullSecrets" -}}
{{- with .Values.imagenes.pullSecrets }}
imagePullSecrets:
{{- toYaml . | nindent 2 }}
{{- end }}
{{- end -}}

{{/* Nombre del Secret con las llaves de componente */}}
{{- define "proxy-cache.secretoLlaves" -}}
{{- default (printf "%s-llaves" .Release.Name) .Values.llaves.secretoExistente -}}
{{- end -}}

{{/* Nombre del Secret con la CA local */}}
{{- define "proxy-cache.secretoCa" -}}
{{- default (printf "%s-ca" .Release.Name) .Values.cache.ca.secretoExistente -}}
{{- end -}}

{{- define "proxy-cache.nodeApiUrl" -}}
{{- $url := required "Falta nodeApi.url, la URL del REST API Node.js" .Values.nodeApi.url -}}
{{- if not (or (hasPrefix "http://" $url) (hasPrefix "https://" $url)) -}}
{{- fail (printf "nodeApi.url debe iniciar con http:// o https://, se recibió %q" $url) -}}
{{- end -}}
{{- $url -}}
{{- end -}}
