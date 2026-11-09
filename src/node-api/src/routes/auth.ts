import { Hono } from 'hono'
import { requireScope, sha256Hex } from '../middleware/require-scope'
import type { AppEnv, Bindings } from '../types'

// Rutas del formulario de autenticación de la UI, bajo /api/auth. Solo entra la llave con alcance ui
export const auth = new Hono<AppEnv>()

// Para todas las rutas de este router, se requiere que la llave tenga el scope 'ui'
auth.use('*', requireScope('ui'))

// Si SESSION_TTL_SECONDS no está o no sirve, una hora
const TTL_POR_DEFECTO = 3600

function ttlSesion(env: Bindings): number {
  const ttl = Number(env.SESSION_TTL_SECONDS)
  return Number.isSafeInteger(ttl) && ttl > 0 ? ttl : TTL_POR_DEFECTO
}

// 32 bytes aleatorios en hexadecimal
function nuevoToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32))
  return [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('')
}

// Ruta POST /login: valida el usuario y la contraseña de una URL y crea una sesión
/**
 * inputs: { url_id, username, password }
 * outputs: { session, expires_in } o 401 si las credenciales no son válidas
 */
auth.post('/login', async (c) => {
  const body = await c.req.json().catch(() => null)
  const urlId = body?.url_id
  const username = body?.username
  const password = body?.password
  if (
    !Number.isSafeInteger(urlId) ||
    typeof username !== 'string' || username === '' ||
    typeof password !== 'string' || password === ''
  ) {
    return c.json({ error: 'Se espera { url_id, username, password }' }, 400)
  }

  // bcrypt corre dentro de Supabase
  const { data: urlUserId, error } = await c.var.supabase.rpc('verificar_url_user', {
    p_url_id: urlId,
    p_username: username,
    p_password: password,
  })
  if (error) {
    console.error('No se pudo verificar el usuario:', error.message)
    return c.json({ error: 'No se pudo verificar el usuario' }, 500)
  }
  if (urlUserId === null) {
    return c.json({ error: 'Usuario o contraseña no válidos' }, 401)
  }

  const ttl = ttlSesion(c.env)
  const session = nuevoToken()
  const { error: errorInsertar } = await c.var.supabase.from('sessions').insert({
    url_id: urlId,
    url_user_id: urlUserId,
    token_hash: await sha256Hex(session),
    expires_at: new Date(Date.now() + ttl * 1000).toISOString(),
  })
  if (errorInsertar) {
    console.error('No se pudo guardar la sesión:', errorInsertar.message)
    return c.json({ error: 'No se pudo crear la sesión' }, 500)
  }

  return c.json({ session, expires_in: ttl })
})

// Ruta POST /logout: borra la sesión, así la caché deja de aceptarla aunque el navegador guarde la cookie
/**
 * inputs: { session }
 * outputs: { ok: true }, también si la sesión ya no existía
 */
auth.post('/logout', async (c) => {
  const body = await c.req.json().catch(() => null)
  const session = body?.session
  if (typeof session !== 'string' || session === '') {
    return c.json({ error: 'Se espera { session }' }, 400)
  }

  const { error } = await c.var.supabase
    .from('sessions')
    .delete()
    .eq('token_hash', await sha256Hex(session))
  if (error) {
    console.error('No se pudo cerrar la sesión:', error.message)
    return c.json({ error: 'No se pudo cerrar la sesión' }, 500)
  }

  return c.json({ ok: true })
})
