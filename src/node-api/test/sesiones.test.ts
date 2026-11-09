import { beforeEach, describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { fakeSupabase } from './fake-supabase'

const env = {
  SUPABASE_URL: 'https://falso.supabase.co',
  SUPABASE_SECRET_KEY: 'falsa',
  SESSION_TTL_SECONDS: '120',
}

let tablas: Record<string, Record<string, unknown>[]>

beforeEach(async () => {
  tablas = {
    component_keys: [
      { scope: 'ui', key_hash: await sha256Hex('llave-ui') },
      { scope: 'cache', key_hash: await sha256Hex('llave-cache') },
    ],
    urls: [
      { id: 4, auth: 'user_password' },
      { id: 8, auth: 'user_password' },
      { id: 7, auth: 'api_key' },
    ],
    // En Supabase es password_hash con bcrypt; el falso compara texto plano
    url_users: [
      { id: 1, url_id: 4, username: 'alonso', password: 'apache-1234' },
      { id: 2, url_id: 8, username: 'alonso', password: 'https-1234' },
      { id: 3, url_id: 7, username: 'alonso', password: 'java-1234' },
    ],
    sessions: [],
  }
})

const post = (ruta: string, llave: string, body: unknown, entorno: Record<string, string> = env, fallar: string[] = []) =>
  createApp(fakeSupabase(tablas, fallar)).request(
    ruta,
    {
      method: 'POST',
      headers: { 'x-api-key': llave, 'content-type': 'application/json' },
      body: typeof body === 'string' ? body : JSON.stringify(body),
    },
    entorno,
  )

const login = (body: unknown, entorno?: Record<string, string>) => post('/api/auth/login', 'llave-ui', body, entorno)
const logout = (body: unknown) => post('/api/auth/logout', 'llave-ui', body)
const verificar = (body: unknown) => post('/api/cache/sessions/verify', 'llave-cache', body)

describe('POST /api/auth/login', () => {
  it('con usuario y contraseña correctos devuelve una sesión y guarda solo su hash', async () => {
    const res = await login({ url_id: 4, username: 'alonso', password: 'apache-1234' })
    expect(res.status).toBe(200)
    const body = (await res.json()) as { session: string; expires_in: number }
    expect(body.session).toMatch(/^[0-9a-f]{64}$/)
    expect(body.expires_in).toBe(120)

    expect(tablas.sessions).toHaveLength(1)
    const fila = tablas.sessions[0]
    expect(fila).toMatchObject({ url_id: 4, url_user_id: 1, token_hash: await sha256Hex(body.session) })
    expect(JSON.stringify(fila)).not.toContain(body.session)
  })

  it('la sesión expira según SESSION_TTL_SECONDS', async () => {
    const antes = Date.now()
    await login({ url_id: 4, username: 'alonso', password: 'apache-1234' })
    const expira = Date.parse(tablas.sessions[0].expires_at as string)
    expect(expira - antes).toBeGreaterThanOrEqual(120_000)
    expect(expira - antes).toBeLessThan(125_000)
  })

  it('sin SESSION_TTL_SECONDS o con uno que no sirve usa una hora', async () => {
    for (const valor of [undefined, 'abc', '0', '-5']) {
      const { SESSION_TTL_SECONDS: _, ...resto } = env
      const entorno = valor === undefined ? resto : { ...resto, SESSION_TTL_SECONDS: valor }
      const body = (await (await login({ url_id: 4, username: 'alonso', password: 'apache-1234' }, entorno)).json()) as {
        expires_in: number
      }
      expect(body.expires_in, String(valor)).toBe(3600)
    }
  })

  it.each([
    ['contraseña equivocada', { url_id: 4, username: 'alonso', password: 'mala' }],
    ['usuario que no existe', { url_id: 4, username: 'nadie', password: 'apache-1234' }],
    ['usuario de otra URL', { url_id: 4, username: 'alonso', password: 'https-1234' }],
    ['URL que no pide user_password', { url_id: 7, username: 'alonso', password: 'java-1234' }],
  ])('responde 401 sin crear sesión: %s', async (_, body) => {
    const res = await login(body)
    expect(res.status).toBe(401)
    expect(tablas.sessions).toHaveLength(0)
  })

  it.each([
    ['sin url_id', { username: 'alonso', password: 'apache-1234' }],
    ['url_id como texto', { url_id: '4', username: 'alonso', password: 'apache-1234' }],
    ['usuario vacío', { url_id: 4, username: '', password: 'apache-1234' }],
    ['sin contraseña', { url_id: 4, username: 'alonso' }],
    ['JSON roto', '{"url_id": 4'],
  ])('responde 400 con un cuerpo que no sirve: %s', async (_, body) => {
    expect((await login(body)).status).toBe(400)
  })

  it('responde 500 si Supabase falla', async () => {
    const body = { url_id: 4, username: 'alonso', password: 'apache-1234' }
    expect((await post('/api/auth/login', 'llave-ui', body, env, ['verificar_url_user'])).status).toBe(500)
    expect((await post('/api/auth/login', 'llave-ui', body, env, ['sessions'])).status).toBe(500)
  })

  it('solo entra la llave de la UI', async () => {
    const body = { url_id: 4, username: 'alonso', password: 'apache-1234' }
    expect((await post('/api/auth/login', 'llave-cache', body)).status).toBe(403)
    expect((await post('/api/auth/login', 'inventada', body)).status).toBe(401)
  })
})

describe('POST /api/cache/sessions/verify', () => {
  const iniciar = async () => {
    const res = await login({ url_id: 4, username: 'alonso', password: 'apache-1234' })
    return ((await res.json()) as { session: string }).session
  }

  it('una sesión recién creada es válida para su URL', async () => {
    const session = await iniciar()
    const res = await verificar({ url_id: 4, session })
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ valid: true })
  })

  it('una sesión de otra URL no es válida', async () => {
    const session = await iniciar()
    expect(await (await verificar({ url_id: 8, session })).json()).toEqual({ valid: false })
  })

  it('una sesión inventada no es válida', async () => {
    expect(await (await verificar({ url_id: 4, session: 'inventada' })).json()).toEqual({ valid: false })
  })

  it('una sesión vencida no es válida', async () => {
    const session = await iniciar()
    tablas.sessions[0].expires_at = new Date(Date.now() - 1000).toISOString()
    expect(await (await verificar({ url_id: 4, session })).json()).toEqual({ valid: false })
  })

  it('después del logout la sesión ya no es válida', async () => {
    const session = await iniciar()
    const res = await logout({ session })
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ ok: true })
    expect(tablas.sessions).toHaveLength(0)
    expect(await (await verificar({ url_id: 4, session })).json()).toEqual({ valid: false })
  })

  it.each([
    ['sin session', { url_id: 4 }],
    ['session vacía', { url_id: 4, session: '' }],
    ['sin url_id', { session: 'abc' }],
    ['JSON roto', '{"url_id": 4'],
  ])('responde 400 con un cuerpo que no sirve: %s', async (_, body) => {
    expect((await verificar(body)).status).toBe(400)
  })

  it('responde 500 si Supabase falla', async () => {
    const res = await post('/api/cache/sessions/verify', 'llave-cache', { url_id: 4, session: 'x' }, env, ['sessions'])
    expect(res.status).toBe(500)
  })

  it('solo entra la llave de la caché', async () => {
    expect((await post('/api/cache/sessions/verify', 'llave-ui', { url_id: 4, session: 'x' })).status).toBe(403)
  })
})

describe('POST /api/auth/logout', () => {
  it('una sesión que no existe también responde ok', async () => {
    expect(await (await logout({ session: 'inventada' })).json()).toEqual({ ok: true })
  })

  it('responde 400 sin session', async () => {
    expect((await logout({})).status).toBe(400)
  })
})
