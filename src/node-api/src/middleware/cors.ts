import { cors as honoCors } from 'hono/cors'
import { createMiddleware } from 'hono/factory'
import type { AppEnv } from '../types'

/*
  CORS para la UI, que corre en otro origen y llama al Worker desde el navegador

  UI_ORIGINS trae los orígenes permitidos separados por coma. El Interceptor y las cachés no usan navegador,
  así que a ellos CORS no les cambia nada. El preflight (OPTIONS) se responde aquí, sin pedir x-api-key
*/
export const cors = createMiddleware<AppEnv>((c, next) => {
  // lee la variable de entorno UI_ORIGINS
  const permitidos = (c.env.UI_ORIGINS ?? '')                           // UI_ORIGINS es opcional xd
    .split(',')
    .map((o) => o.trim())                                               // elimina espacios en blanco
    .filter(Boolean)                                                    // elimina cadenas vacías

  return honoCors({                                                     // obtiene el header "origin" y lo compara con los permitidos
    origin: (origin) => (permitidos.includes(origin) ? origin : null),
    allowMethods: ['GET', 'POST', 'OPTIONS'],
    allowHeaders: ['Content-Type', 'x-api-key'],
    maxAge: 86400,
  })(c, next)
})
