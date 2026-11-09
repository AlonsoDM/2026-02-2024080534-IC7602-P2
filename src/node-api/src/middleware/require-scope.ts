import { createMiddleware } from 'hono/factory'
import type { AppEnv, Scope } from '../types'

/*
  Middleware que valida que la petición tenga un x-api-key con el alcance requerido

  La key de componente se pasa en el header x-api-key y se valida contra la tabla component_keys de Supabase.
*/

// calcula SHA-256 en hexadecimal igual que en Supabase
export async function sha256Hex(text: string): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('')
}

// Deja pasar solo a las llaves de componente con este alcance
export const requireScope = (scope: Scope) =>
  // c es el contexto de Hono, que tiene c.env y c.var con las variables del entorno y del middleware
  createMiddleware<AppEnv>(async (c, next) => {
    const key = c.req.header('x-api-key')
    if (!key) {
      return c.json({ error: 'Falta el header x-api-key' }, 401)
    }

    const { data, error } = await c.var.supabase
      .from('component_keys')
      .select('scope')
      .eq('key_hash', await sha256Hex(key))
      .maybeSingle()

    if (error) {
      console.error('No se pudo validar la llave de componente:', error.message)
      return c.json({ error: 'No se pudo validar la llave' }, 500)
    }
    if (!data) {
      return c.json({ error: 'Llave no válida' }, 401)
    }
    if (data.scope !== scope) {
      return c.json({ error: 'La llave no tiene permiso para esta ruta' }, 403)
    }

    c.set('scope', data.scope)
    await next()
  })
