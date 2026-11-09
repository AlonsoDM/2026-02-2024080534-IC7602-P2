#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>
#include <time.h>
#include "conexion.h"

// Iniciar antes de crear hilos. La CA se conserva hasta tls_liberar.
int tls_iniciar(const char *cert_file, const char *key_file);
// Crea la sesión asociada al socket; todavía no ejecuta SSL_accept.
// El llamador entrega el SSL a Conexion y conserva la propiedad del descriptor.
SSL *tls_crear(int fd);
// Completa el handshake en hasta 10 segundos y restaura el modo del socket.
// El llamador cierra la Conexion tanto si tiene éxito como si falla.
int tls_aceptar(Conexion *conexion);
// Llamar cuando ya no queden conexiones TLS en uso.
void tls_liberar(void);

// También pública para probar renovación con tiempo controlado.
// Exige un patrón o host TLS habilitado y devuelve una referencia propia: SSL_CTX_free.
SSL_CTX *tls_ensure_cert(const char *patron, time_t ahora);

#endif
