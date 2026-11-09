#ifndef SERVIDOR_H
#define SERVIDOR_H

// Devuelve el socket de escucha o -1 si falla. El llamador lo cierra.
int servidor_abrir(int puerto);
int servidor_atender(int servidor, int servidor_tls);

#endif
