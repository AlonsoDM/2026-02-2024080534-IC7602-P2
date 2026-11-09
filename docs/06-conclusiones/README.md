# Conclusiones y recomendaciones

## Conclusiones

1. Se usó un hilo por conexión porque el índice de la caché tiene que ser uno solo y compartido; con procesos distintos cada uno tendría el suyo. El mutex protege solo el índice y `helgrind` no reporta carreras en `cache.c`.
2. Para guardar un recurso se escribe en un archivo temporal y se renombra, así nadie lee un archivo a medias. Un archivo ya abierto se sigue leyendo aunque otro hilo lo borre.
3. El límite de tamaño se garantiza antes de guardar: se sacan primero los vencidos y luego víctimas de esa URL según su política. En la prueba de punta a punta, 300 PNG dejaron `externo-http.test` en 2 095 562 de 2 097 152 bytes.
4. LRU y MRU usan un contador de usos y no la hora, porque dos accesos en el mismo segundo empatarían. LFU sí puede empatar y se desempata con LRU.
5. El TTL necesita un hilo que purgue, porque lo vencido que nadie vuelve a pedir seguiría ocupando espacio. El vencimiento se mide con `CLOCK_MONOTONIC` para que un cambio de hora no lo afecte.
6. Solo se cachea un `GET` con `200` de un tipo configurado, y nada con `Set-Cookie`, `private` o `no-store`. Si la ruta no tiene extensión se usa el `Content-Type`, y así el REST API Java también se cachea.
7. El Interceptor da la misma IP de caché a todos los dominios de una zona, así que la caché distingue el sitio por el header `Host`. Por eso la clave de caché lleva el host y al origen se le manda su propio `Host`.
8. El origen lo escribe el usuario en la UI, así que no es de confianza. libcurl solo acepta `http` y `https` (con un origen `file:///` se leerían archivos del pod), no sigue redirects y no recibe `Accept-Encoding`.
9. La UI hace su CRUD directo en Supabase protegida por RLS, y la caché y el Interceptor solo hablan con el REST API con una llave de componente con alcance. La llave de la UI se ve en el navegador, por eso su alcance es mínimo.
10. En usuario y contraseña la caché nunca ve la contraseña: la UI valida contra la base, devuelve la sesión en el `callback` y la caché la verifica y la deja en una cookie `HttpOnly` de su propio dominio. La UI no comprueba que el host del `callback` pertenezca a la URL; la sesión solo vale para esa URL y expira.
11. La caché descarta las URLs sin `tls_enabled` y el Worker no lo mandaba, así que cargaba 0 URLs y respondía `404` a todo. Las pruebas con un API falso no lo vieron; salió al conectar con la ruta real.
12. Para SSL termination la caché emite en memoria un certificado por host según el SNI, firmado por una CA local, y por eso `curl --cacert` funciona sin `-k`. Un wildcard cubre un solo nivel, así que el dominio raíz necesita su certificado exacto.
13. Una Elastic IP por zona en una sola EC2 se logra con IP privadas secundarias y `externalIPs` en cada `Service`, y con `externalTrafficPolicy: Local` para que el Interceptor vea la IP del cliente y no la del nodo.
14. Terraform, Helm y `just` dejan la instalación en `just infra` y `just up`. Probar el chart y la prueba de punta a punta en un k3s dentro de Docker permitió validar casi todo sin gastar créditos de AWS, y al correrla en AWS pasó completa.
15. Los hitos de integración del 11/10, 15/10 y 22/10 no se cumplieron y casi todo se juntó en las últimas semanas. El reenvío al origen estuvo semanas probado solo contra un servidor eco.

## Recomendaciones

1. Correr la caché contra la ruta real `GET /api/cache/config` desde la primera semana y no contra un API falso. Así lo de `tls_enabled` se habría visto en días.
2. Tener los orígenes reales (API Java y Apache) antes de cerrar el reenvío al origen, para no probarlo contra servidores sustitutos.
3. Fijar un hito de integración cada semana con revisión entre pares, como recomienda el enunciado, en lugar de dejarla para el final.
4. Correr las pruebas de Criterion y la de punta a punta en k3s en cada PR. Hoy son comandos manuales, y tras integrar la caché en disco dos pruebas dejaron de compilar sin que nadie lo notara.
5. Probar la geolocalización con clientes reales de otros países, por ejemplo con una VPN. En AWS se probó con la IP real de Costa Rica; US, ES y GB solo con `client_ip` simulado.
6. Soportar EDNS Client Subnet en el Interceptor. Hoy geolocaliza la IP de quien le pregunta, que suele ser el resolver y no la persona.
7. Pasar los cuerpos grandes al disco por pedazos en vez de armarlos completos en memoria. Hoy un recurso de más de 8 MB da `502`.
8. Atender varias peticiones por conexión y usar un pool de hilos en vez de un hilo por conexión si la carga sube.
9. Guardar el disco de la caché en un volumen persistente y reconstruir el índice al arrancar. Con `emptyDir` cada reinicio empieza vacío.
10. Agregar a la UI un campo para activar HTTPS en cada URL. Hoy `tls_enabled` solo se cambia en Supabase.
11. Hacer que `POST /api/auth/login` compare el host del `callback` con el patrón de la URL antes de devolver la sesión.
12. Usar certificados de una CA pública en lugar de la CA local, para que ningún cliente tenga que instalar `rootCA.pem` o usar `-k`.
13. Mover las llaves a un gestor de secretos y no cargar `seed.sql` fuera de desarrollo, porque sus llaves y contraseñas (`dev-*`) son públicas.
14. Para más de 4 zonas, repartir las cachés en varias regiones o usar un balanceador. AWS da 5 Elastic IP por región y una es del Interceptor.
15. Actualizar la documentación en el mismo PR que cambia el comportamiento. Varias guías siguieron diciendo que `cache_buscar` estaba vacía mucho después de implementarla.
