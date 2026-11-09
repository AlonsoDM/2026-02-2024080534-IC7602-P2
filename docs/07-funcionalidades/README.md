# Tabla de funcionalidades

Qué funciona y qué no, por cada aspecto que pide el enunciado. Todo funciona.

## DNS Interceptor

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| Intercepta las consultas DNS de dominios específicos y las redirige a una caché zonal | Sí |
| Elige la caché zonal según el lugar de procedencia de la petición, con la base IP to Country | Sí |
| Adaptado del primer proyecto, en GNU C, ejecutado en Kubernetes y automatizado con Helm Charts | Sí |

## Zonal Cache

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| Caché HTTP en disco, con tamaño máximo por URL | Sí |
| Obtiene su configuración del REST API Node.js, que la lee de Supabase (en lugar de Firebase) | Sí |
| Algoritmos de reemplazo LRU, LFU, FIFO, MRU y Random | Sí |
| TTL por tipo de archivo | Sí |
| Autenticación con el header `x-api-key` | Sí |
| Autenticación con usuario y contraseña, llamando a la UI, y verificación de la sesión | Sí |
| SSL termination | Sí |
| Flujo de la petición: validar seguridad, buscar en caché, pedir al origen, guardar y servir | Sí |
| Implementada en GNU C, ejecutada en Kubernetes y automatizada con Helm Charts | Sí |

## REST API Node.js / UI

### REST API Node.js

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| Endpoints que necesitan la UI y las cachés | Sí |
| Deployments automáticos desde GitHub (Cloudflare Workers, en lugar de Vercel) | Sí |
| Mecanismo de autenticación y autorización, investigado, implementado y documentado | Sí |

### UI

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| React, Vite y Tailwind CSS, con deployments automáticos desde GitHub (Cloudflare Pages, en lugar de Vercel) | Sí |
| Registro, y agregar y eliminar dominios, verificados con un registro TXT | Sí |
| Crear, modificar y eliminar URLs, con wildcards, tamaño de caché, tipos de archivo con su TTL y política de reemplazo | Sí |
| Autenticación de cada URL: API Keys (crear y eliminar) o usuarios y contraseñas (crear, modificar y eliminar) | Sí |
| Formulario de autenticación que usan las cachés zonales | Sí |
| Cerrar sesión | Sí |

## REST API / Apache Server

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| REST API en Java con los verbos GET, POST, PUT y DELETE | Sí |
| Servidor Apache que sirve una página HTML básica | Sí |
| El REST API y el Apache ejecutados en Kubernetes y automatizados con Helm Charts | Sí |
| Probado detrás de la caché con el REST API, el Apache, un sitio externo con HTTP y uno con HTTPS | Sí |

## Automatización

| Aspecto pedido | ¿Funciona? |
| --- | --- |
| La infraestructura en AWS se crea con Terraform | Sí |
| Sin valores quemados: la configuración se inyecta con variables de entorno | Sí |
