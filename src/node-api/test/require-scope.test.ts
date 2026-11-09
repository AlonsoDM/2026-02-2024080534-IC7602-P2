import { beforeAll, describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { fakeSupabase } from './fake-supabase'

const env = { SUPABASE_URL: 'https://falso.supabase.co', SUPABASE_SECRET_KEY: 'falsa' }

let app: ReturnType<typeof createApp>

beforeAll(async () => {
  app = createApp(
    fakeSupabase({
      component_keys: [
        { scope: 'ui', key_hash: await sha256Hex('llave-ui') },
        { scope: 'interceptor', key_hash: await sha256Hex('llave-interceptor') },
        { scope: 'cache', key_hash: await sha256Hex('llave-cache') },
      ],
      urls: [],
    }),
  )
})

const pedir = (ruta: string, llave?: string, method = 'GET') =>
  app.request(ruta, { method, headers: llave ? { 'x-api-key': llave } : {} }, env)

describe('sha256Hex', () => {
  it('da el mismo hash que encode(digest(..., sha256), hex) de Supabase', async () => {
    expect(await sha256Hex('abc')).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
  })
})

describe('requireScope', () => {
  it('/health no pide llave', async () => {
    const res = await pedir('/health')
    expect(res.status).toBe(200)
  })

  it('responde 401 si falta x-api-key', async () => {
    const res = await pedir('/api/cache/config')
    expect(res.status).toBe(401)
    expect(await res.json()).toEqual({ error: 'Falta el header x-api-key' })
  })

  it('responde 401 si la llave no existe', async () => {
    const res = await pedir('/api/cache/config', 'llave-inventada')
    expect(res.status).toBe(401)
    expect(await res.json()).toEqual({ error: 'Llave no válida' })
  })

  it.each([
    ['/api/cache/config', 'llave-ui'],
    ['/api/cache/config', 'llave-interceptor'],
    ['/api/interceptor/resolve', 'llave-cache'],
    ['/api/interceptor/resolve', 'llave-ui'],
    ['/api/auth/login', 'llave-cache'],
    ['/api/domains/1/verify', 'llave-interceptor'],
  ])('responde 403 en %s con %s', async (ruta, llave) => {
    const res = await pedir(ruta, llave, ruta.startsWith('/api/cache') ? 'GET' : 'POST')
    expect(res.status).toBe(403)
  })

  it.each([
    ['/api/cache/config', 'llave-cache', 'GET', 200],
    ['/api/interceptor/resolve?domain=apache.test&client_ip=8.8.8.8', 'llave-interceptor', 'GET', 200],
    // Pasa el alcance y el handler responde 400 porque no lleva cuerpo
    ['/api/auth/login', 'llave-ui', 'POST', 400],
    // Las rutas restantes todavía no tienen handler.
    ['/api/domains/1/verify', 'llave-ui', 'POST', 404],
  ])('deja pasar %s con %s', async (ruta, llave, method, status) => {
    const res = await pedir(ruta, llave, method)
    expect(res.status).toBe(status)
  })

  it('responde 500 si Supabase falla, y no lo confunde con una llave inválida', async () => {
    const caido = createApp(fakeSupabase({}, ['component_keys']))
    const res = await caido.request('/api/cache/config', { headers: { 'x-api-key': 'llave-cache' } }, env)
    expect(res.status).toBe(500)
  })
})
