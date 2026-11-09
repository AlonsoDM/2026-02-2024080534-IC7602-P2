import { beforeAll, describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { fakeSupabase } from './fake-supabase'

const env = { SUPABASE_URL: 'https://falso.supabase.co', SUPABASE_SECRET_KEY: 'falsa' }

let llaves: Record<string, unknown>[]

beforeAll(async () => {
  llaves = [{ scope: 'cache', key_hash: await sha256Hex('llave-cache') }]
})

// Filas como las devuelve Supabase: con la relación domains y url_file_types sin ordenar
const urls = [
  {
    id: 9,
    pattern: '*.marca.com',
    tls_enabled: false,
    origin: 'https://origen.marca.com',
    cache_size_bytes: 2097152,
    replacement_policy: 'fifo',
    auth: 'user_password',
    url_file_types: [],
    domains: { verified: true },
  },
  {
    id: 7,
    pattern: 'api.marca.com',
    tls_enabled: true,
    origin: 'http://java-api:8080',
    cache_size_bytes: 1048576,
    replacement_policy: 'lru',
    auth: 'api_key',
    url_file_types: [
      { extension: 'png', ttl_seconds: 600 },
      { extension: 'json', ttl_seconds: 30 },
    ],
    domains: { verified: true },
  },
  {
    id: 8,
    pattern: 'sin-verificar.com',
    tls_enabled: true,
    origin: 'http://apache:80',
    cache_size_bytes: 1024,
    replacement_policy: 'lru',
    auth: 'none',
    url_file_types: [{ extension: 'html', ttl_seconds: 60 }],
    domains: { verified: false },
  },
]

const pedirConfig = (supabase = fakeSupabase({ component_keys: llaves, urls })) =>
  createApp(supabase).request('/api/cache/config', { headers: { 'x-api-key': 'llave-cache' } }, env)

describe('GET /api/cache/config', () => {
  it('devuelve el formato de la sección 5 del README', async () => {
    const res = await pedirConfig()
    expect(res.status).toBe(200)
    expect(res.headers.get('content-type')).toContain('application/json')

    const body = (await res.json()) as { urls: Record<string, unknown>[] }
    expect(body.urls[0]).toEqual({
      id: 7,
      pattern: 'api.marca.com',
      tls_enabled: true,
      origin: 'http://java-api:8080',
      cache_size_bytes: 1048576,
      replacement_policy: 'lru',
      auth: 'api_key',
      file_types: [
        { extension: 'json', ttl_seconds: 30 },
        { extension: 'png', ttl_seconds: 600 },
      ],
    })
  })

  it('solo incluye URLs de dominios verificados, ordenadas por id', async () => {
    const body = (await (await pedirConfig()).json()) as { urls: { id: number }[] }
    expect(body.urls.map((u) => u.id)).toEqual([7, 9])
  })

  it('no deja salir columnas que no son del contrato', async () => {
    const body = (await (await pedirConfig()).json()) as { urls: Record<string, unknown>[] }
    for (const u of body.urls) {
      expect(Object.keys(u).sort()).toEqual(
        ['auth', 'cache_size_bytes', 'file_types', 'id', 'origin', 'pattern', 'replacement_policy', 'tls_enabled'],
      )
    }
  })

  // Sin tls_enabled la caché descarta cada URL y queda con 0
  it('manda tls_enabled como booleano en cada URL', async () => {
    const body = (await (await pedirConfig()).json()) as { urls: { id: number; tls_enabled: unknown }[] }
    expect(body.urls.map((u) => [u.id, u.tls_enabled])).toEqual([[7, true], [9, false]])
  })

  it('una URL sin tipos de archivo lleva file_types vacío', async () => {
    const body = (await (await pedirConfig()).json()) as { urls: { id: number; file_types: unknown[] }[] }
    expect(body.urls.find((u) => u.id === 9)?.file_types).toEqual([])
  })

  it('responde { urls: [] } si no hay URLs', async () => {
    const res = await pedirConfig(fakeSupabase({ component_keys: llaves, urls: [] }))
    expect(await res.json()).toEqual({ urls: [] })
  })

  it('responde 500 si Supabase falla al leer las URLs', async () => {
    const res = await pedirConfig(fakeSupabase({ component_keys: llaves, urls }, ['urls']))
    expect(res.status).toBe(500)
  })
})
