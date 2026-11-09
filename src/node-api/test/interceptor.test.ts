import { beforeAll, describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { fakeSupabase } from './fake-supabase'

const env = { SUPABASE_URL: 'https://falso.supabase.co', SUPABASE_SECRET_KEY: 'falsa' }
let llaves: Record<string, unknown>[]
beforeAll(async () => {
  llaves = [{ scope: 'interceptor', key_hash: await sha256Hex('llave-interceptor') }]
})

const urls = [
  { pattern: 'exacto.test', domains: { verified: true } },
  { pattern: '*.marca.com', domains: { verified: true } },
  { pattern: '*.sin-verificar.test', domains: { verified: false } },
]
const zones = [
  { code: 'US', ip: '203.0.113.12', countries: ['US', 'CA'] },
  { code: 'ES', ip: '203.0.113.13', countries: ['ES', 'PT'] },
  { code: 'GB', ip: '203.0.113.24', countries: ['GB', 'IE'] },
]
const paises = { '8.8.8.8': 'US', '80.58.61.250': 'ES', '81.2.69.160': 'GB', '1.2.3.4': 'AU' }

function pedir(domain = 'exacto.test', ip = '8.8.8.8', tablas = { urls, zones }, fallar: string[] = []) {
  return createApp(fakeSupabase({ component_keys: llaves, ...tablas }, fallar, paises))
    .request(`/api/interceptor/resolve?${new URLSearchParams({ domain, client_ip: ip })}`,
      { headers: { 'x-api-key': 'llave-interceptor' } }, env)
}

describe('GET /api/interceptor/resolve', () => {
  it.each([
    ['8.8.8.8', 'US', '203.0.113.12'],
    ['80.58.61.250', 'ES', '203.0.113.13'],
    ['81.2.69.160', 'GB', '203.0.113.24'],
  ])('selecciona la caché según client_ip %s', async (ip, zone, address) => {
    const res = await pedir('exacto.test', ip)
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ exists: true, zone, address })
  })

  it.each(['marca.com', 'www.marca.com', 'a.b.marca.com', 'MARCA.COM.'])('el wildcard incluye %s', async (domain) => {
    const res = await pedir(domain)
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ exists: true, zone: 'US', address: '203.0.113.12' })
  })

  it.each(['otro.test', 'api.exacto.test', 'otramarca.com', 'marca.com.otro.test', 'sin-verificar.test', 'api.sin-verificar.test'])
    ('no intercepta %s ni consulta GEO si el nombre no coincide', async (domain) => {
      const res = await pedir(domain, '8.8.8.8', { urls, zones }, ['pais_de_ip', 'zones'])
      expect(res.status).toBe(200)
      expect(await res.json()).toEqual({ exists: false })
    })

  it('normaliza mayúsculas y el punto final de un nombre exacto', async () => {
    expect((await (await pedir('EXACTO.TEST.')).json())).toEqual({
      exists: true, zone: 'US', address: '203.0.113.12',
    })
  })

  it.each(['', '.marca.com', 'a..marca.com', '-mal.test', 'mal-.test', 'mal_.test', '*.marca.com', 'marca.com:80', 'https://marca.com', 'marca.com\n', `${'a'.repeat(64)}.test`, `${'a'.repeat(63)}.${'b'.repeat(63)}.${'c'.repeat(63)}.${'d'.repeat(63)}`])
    ('rechaza el dominio inválido %s', async (domain) => {
      expect((await pedir(domain)).status).toBe(400)
    })

  it.each(['', '256.1.2.3', '1.2.3', '01.2.3.4', '::1', '8.8.8.8/24', 'no-es-ip', '8.8.8.8\n'])
    ('rechaza client_ip inválido %s', async (ip) => {
      expect((await pedir('exacto.test', ip)).status).toBe(400)
    })

  it.each(['domain=exacto.test', 'client_ip=8.8.8.8', ''])('rechaza parámetros ausentes: %s', async (query) => {
    const app = createApp(fakeSupabase({ component_keys: llaves }))
    const res = await app.request(`/api/interceptor/resolve?${query}`,
      { headers: { 'x-api-key': 'llave-interceptor' } }, env)
    expect(res.status).toBe(400)
  })

  it.each(['127.0.0.1', '1.2.3.4'])('responde error para país desconocido o sin zona: %s', async (ip) => {
    const res = await pedir('exacto.test', ip)
    expect(res.status).toBe(500)
    expect(await res.json()).not.toHaveProperty('exists')
  })

  it('quitar un país de sus zonas produce error sin cambiar el código', async () => {
    const res = await pedir('exacto.test', '8.8.8.8', {
      urls, zones: zones.map((zone) => ({ ...zone, countries: zone.countries.filter((country) => country !== 'US') })),
    })
    expect(res.status).toBe(500)
  })

  it('selecciona la primera zona que incluye el país', async () => {
    const res = await pedir('exacto.test', '8.8.8.8', {
      urls, zones: [zones[1], zones[0], { code: 'OTRA', ip: '203.0.113.99', countries: ['US'] }],
    })
    expect(await res.json()).toEqual({ exists: true, zone: 'US', address: '203.0.113.12' })
  })

  it.each(['::1', '203.0.113.12/24', 'invalida', ''])('rechaza la IP de caché incompatible: %s', async (ip) => {
    const res = await pedir('exacto.test', '8.8.8.8', { urls, zones: [{ ...zones[0], ip }] })
    expect(res.status).toBe(500)
  })

  it.each(['urls', 'pais_de_ip', 'zones'])('responde 500 si falla %s', async (tabla) => {
    const res = await pedir('exacto.test', '8.8.8.8', { urls, zones }, [tabla])
    expect(res.status).toBe(500)
    expect(await res.json()).not.toHaveProperty('exists')
  })
})
