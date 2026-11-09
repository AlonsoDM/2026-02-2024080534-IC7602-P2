import { beforeAll, describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { fakeSupabase } from './fake-supabase'

const env = { SUPABASE_URL: 'https://falso.supabase.co', SUPABASE_SECRET_KEY: 'falsa' }

let tablas: Record<string, Record<string, unknown>[]>

beforeAll(async () => {
  tablas = {
    component_keys: [{ scope: 'cache', key_hash: await sha256Hex('llave-cache') }],
    // Solo se guarda el hash, igual que en el seed
    api_keys: [
      { id: 1, url_id: 7, key_hash: await sha256Hex('dev-java-key') },
      { id: 2, url_id: 8, key_hash: await sha256Hex('dev-http-key') },
    ],
  }
})

const verificar = (body: unknown, supabase = fakeSupabase(tablas)) =>
  createApp(supabase).request(
    '/api/cache/api-keys/verify',
    {
      method: 'POST',
      headers: { 'x-api-key': 'llave-cache', 'content-type': 'application/json' },
      body: typeof body === 'string' ? body : JSON.stringify(body),
    },
    env,
  )

describe('POST /api/cache/api-keys/verify', () => {
  it('una llave válida de esa URL da valid: true', async () => {
    const res = await verificar({ url_id: 7, key: 'dev-java-key' })
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ valid: true })
  })

  it('una llave que no existe da valid: false', async () => {
    const res = await verificar({ url_id: 7, key: 'inventada' })
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ valid: false })
  })

  it('una llave de otra URL da valid: false', async () => {
    const res = await verificar({ url_id: 7, key: 'dev-http-key' })
    expect(await res.json()).toEqual({ valid: false })
  })

  it.each([
    ['sin key', { url_id: 7 }],
    ['key vacía', { url_id: 7, key: '' }],
    ['sin url_id', { key: 'dev-java-key' }],
    ['url_id como texto', { url_id: '7', key: 'dev-java-key' }],
    ['JSON roto', '{"url_id": 7'],
  ])('responde 400 con un cuerpo que no sirve: %s', async (_, body) => {
    const res = await verificar(body)
    expect(res.status).toBe(400)
  })

  it('responde 500 si Supabase falla', async () => {
    const res = await verificar({ url_id: 7, key: 'dev-java-key' }, fakeSupabase(tablas, ['api_keys']))
    expect(res.status).toBe(500)
  })

  it('pide la llave de componente con alcance cache', async () => {
    const res = await createApp(fakeSupabase(tablas)).request(
      '/api/cache/api-keys/verify',
      { method: 'POST', body: JSON.stringify({ url_id: 7, key: 'dev-java-key' }) },
      env,
    )
    expect(res.status).toBe(401)
  })
})
