import { Hono } from 'hono'
import { requireScope, sha256Hex } from '../middleware/require-scope'
import type { AppEnv } from '../types'

/*
  Obtiene la config de la cache desde Supabase y la devuelve en JSON
  La config se refresca cada CONFIG_REFRESH_SECONDS
*/

// Rutas del Zonal Cache, bajo /api/cache. Solo entra la llave con alcance cache
export const cache = new Hono<AppEnv>()

// Para todas las rutas de cache se requiere que la llave tenga el scope 'cache'
cache.use('*', requireScope('cache'))

// Ruta GET /config: devuelve la configuración de la cache desde Supabase
/**
 * inputs: x-api-key con una llave de alcance cache
 * outputs: { urls: [ { id, pattern, tls_enabled, origin, cache_size_bytes, replacement_policy, auth, file_types: [ { extension, ttl_seconds } ] } ] }
 */
cache.get('/config', async (c) => {

  // De la tabla 'urls' de Supabase se obtiene la configuración de la cache, incluyendo tipos de files de la tabla 'url_file_types' y dominios verificados de la tabla 'domains'
  const { data, error } = await c.var.supabase
    .from('urls')
    .select(
      'id, pattern, tls_enabled, origin, cache_size_bytes, replacement_policy, auth, ' +
        'url_file_types (extension, ttl_seconds), domains!inner (verified)',
    )
    .eq('domains.verified', true)                                               // Solo las urls de dominios verificados
    .order('id')

  if (error) {
    console.error('No se pudo leer la configuración:', error.message)
    return c.json({ error: 'No se pudo leer la configuración' }, 500)
  }

  // transforma a formato JSON mas limpio
  const urls = (data as unknown as UrlFila[]).map((u) => ({
    id: u.id,
    pattern: u.pattern,
    tls_enabled: u.tls_enabled,
    origin: u.origin,
    cache_size_bytes: u.cache_size_bytes,
    replacement_policy: u.replacement_policy,
    auth: u.auth,
    file_types: [...u.url_file_types]
      .sort((a, b) => a.extension.localeCompare(b.extension))
      .map((t) => ({ extension: t.extension, ttl_seconds: t.ttl_seconds })),
  }))

  return c.json({ urls })
})

// Ruta POST /api-keys/verify: verifica que la x-api-key que mando un cliente a la cache es de esa URL
/**
 * inputs: { url_id, key }
 * outputs: { valid: boolean }
 */
cache.post('/api-keys/verify', async (c) => {
  const body = await c.req.json().catch(() => null)
  const urlId = body?.url_id
  const key = body?.key
  if (!Number.isSafeInteger(urlId) || typeof key !== 'string' || key === '') {
    return c.json({ error: 'Se espera { url_id, key }' }, 400)
  }

  // Se filtra también por url_id xq una llave de otra URL no sirve
  const { data, error } = await c.var.supabase
    .from('api_keys')
    .select('id')
    .eq('key_hash', await sha256Hex(key))
    .eq('url_id', urlId)
    .maybeSingle()

  if (error) {
    console.error('No se pudo verificar la API Key:', error.message)
    return c.json({ error: 'No se pudo verificar la llave' }, 500)
  }

  return c.json({ valid: data !== null })
})

// Ruta POST /sessions/verify: verifica que la sesión que trae un cliente a la cache es de esa URL y no ha expirado
/**
 * inputs: { url_id, session }
 * outputs: { valid: boolean }
 */
cache.post('/sessions/verify', async (c) => {
  const body = await c.req.json().catch(() => null)
  const urlId = body?.url_id
  const session = body?.session
  if (!Number.isSafeInteger(urlId) || typeof session !== 'string' || session === '') {
    return c.json({ error: 'Se espera { url_id, session }' }, 400)
  }

  // Igual que las API Keys, una sesión de otra URL no sirve
  const { data, error } = await c.var.supabase
    .from('sessions')
    .select('id')
    .eq('token_hash', await sha256Hex(session))
    .eq('url_id', urlId)
    .gt('expires_at', new Date().toISOString())
    .maybeSingle()

  if (error) {
    console.error('No se pudo verificar la sesión:', error.message)
    return c.json({ error: 'No se pudo verificar la sesión' }, 500)
  }

  return c.json({ valid: data !== null })
})

// Una fila de urls con sus tipos de archivo, tal como la devuelve Supabase
type UrlFila = {
  id: number
  pattern: string
  tls_enabled: boolean
  origin: string
  cache_size_bytes: number
  replacement_policy: 'lru' | 'lfu' | 'fifo' | 'mru' | 'random'
  auth: 'none' | 'api_key' | 'user_password'
  url_file_types: { extension: string; ttl_seconds: number }[]
}
