import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createApp } from '../src/index'
import { sha256Hex } from '../src/middleware/require-scope'
import { valorTxt } from '../src/routes/domains'
import { fakeSupabase } from './fake-supabase'

const env = { SUPABASE_URL: 'https://falso.supabase.co', SUPABASE_SECRET_KEY: 'falsa' }
const TOKEN = 'abc123token'

let tablas: Record<string, Record<string, unknown>[]>

beforeEach(async () => {
  tablas = {
    component_keys: [
      { scope: 'ui', key_hash: await sha256Hex('llave-ui') },
      { scope: 'cache', key_hash: await sha256Hex('llave-cache') },
    ],
    domains: [
      { id: 1, name: 'marca.com', txt_token: TOKEN, verified: false, verified_at: null },
      { id: 2, name: 'ya.com', txt_token: 'otro', verified: true, verified_at: '2026-11-01T00:00:00Z' },
    ],
  }
})

afterEach(() => {
  vi.unstubAllGlobals()
})

// Simula la respuesta JSON de Cloudflare DoH
function doh(respuesta: { Status: number; Answer?: { type: number; data: string }[] }, ok = true) {
  const fetchFalso = vi.fn().mockResolvedValue({ ok, status: ok ? 200 : 500, json: async () => respuesta })
  vi.stubGlobal('fetch', fetchFalso)
  return fetchFalso
}

const txt = (valor: string) => ({ type: 16, data: valor })

const verificar = (id: string, llave = 'llave-ui', fallar: string[] = []) =>
  createApp(fakeSupabase(tablas, fallar)).request(
    `/api/domains/${id}/verify`,
    { method: 'POST', headers: { 'x-api-key': llave } },
    env,
  )

describe('valorTxt', () => {
  it('quita las comillas', () => {
    expect(valorTxt('"abc"')).toBe('abc')
  })
  it('junta los trozos de un TXT largo', () => {
    expect(valorTxt('"abc" "def"')).toBe('abcdef')
  })
})

describe('POST /api/domains/:id/verify', () => {
  it('rechaza sin llave y con la llave de otro alcance', async () => {
    const sinLlave = await createApp(fakeSupabase(tablas)).request('/api/domains/1/verify', { method: 'POST' }, env)
    expect(sinLlave.status).toBe(401)
    expect((await verificar('1', 'llave-cache')).status).toBe(403)
  })

  it('responde 400 con un id que no es un entero positivo', async () => {
    for (const id of ['abc', '0', '-3', '1.5']) {
      expect((await verificar(id)).status).toBe(400)
    }
  })

  it('responde 404 si el dominio no existe', async () => {
    doh({ Status: 0 })
    expect((await verificar('99')).status).toBe(404)
  })

  it('un dominio ya verificado responde true sin consultar el DNS', async () => {
    const fetchFalso = doh({ Status: 0 })
    const res = await verificar('2')
    expect(await res.json()).toEqual({ verified: true })
    expect(fetchFalso).not.toHaveBeenCalled()
  })

  it('verifica el dominio si el TXT contiene su token', async () => {
    const fetchFalso = doh({ Status: 0, Answer: [txt('"v=spf1 -all"'), txt(`"${TOKEN}"`)] })
    const res = await verificar('1')

    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ verified: true })
    expect(tablas.domains[0].verified).toBe(true)
    expect(tablas.domains[0].verified_at).toBeTruthy()

    const url = String(fetchFalso.mock.calls[0][0])
    expect(url).toContain('name=marca.com')
    expect(url).toContain('type=TXT')
    expect(fetchFalso.mock.calls[0][1]).toEqual({ headers: { accept: 'application/dns-json' } })
  })

  it('acepta un token partido en trozos', async () => {
    doh({ Status: 0, Answer: [txt('"abc123" "token"')] })
    expect(await (await verificar('1')).json()).toEqual({ verified: true })
  })

  it('no verifica si ningún TXT trae el token', async () => {
    doh({ Status: 0, Answer: [txt('"otro-valor"')] })
    const res = await verificar('1')
    expect(res.status).toBe(200)
    expect(await res.json()).toEqual({ verified: false })
    expect(tablas.domains[0].verified).toBe(false)
    expect(tablas.domains[0].verified_at).toBeNull()
  })

  it('no verifica si el token solo aparece dentro de otro texto', async () => {
    doh({ Status: 0, Answer: [txt(`"prefijo-${TOKEN}"`)] })
    expect(await (await verificar('1')).json()).toEqual({ verified: false })
  })

  it('un dominio sin registros TXT (NXDOMAIN) queda sin verificar', async () => {
    doh({ Status: 3 })
    expect(await (await verificar('1')).json()).toEqual({ verified: false })
    expect(tablas.domains[0].verified).toBe(false)
  })

  it('responde 502 si el resolver falla y no verifica', async () => {
    doh({ Status: 2 })
    expect((await verificar('1')).status).toBe(502)
    doh({ Status: 0 }, false)
    expect((await verificar('1')).status).toBe(502)
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('sin red')))
    expect((await verificar('1')).status).toBe(502)
    expect(tablas.domains[0].verified).toBe(false)
  })

  it('responde 500 si Supabase falla al leer o al guardar', async () => {
    doh({ Status: 0, Answer: [txt(`"${TOKEN}"`)] })
    expect((await verificar('1', 'llave-ui', ['domains'])).status).toBe(500)
  })
})
