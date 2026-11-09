import { describe, expect, it } from 'vitest'
import { createApp } from '../src/index'
import { fakeSupabase } from './fake-supabase'

const env = {
  SUPABASE_URL: 'https://falso.supabase.co',
  SUPABASE_SECRET_KEY: 'falsa',
  UI_ORIGINS: 'http://localhost:5173, https://zonal-ui.pages.dev',
}

const app = createApp(fakeSupabase({ component_keys: [] }))

const preflight = (origin: string) =>
  app.request(
    '/api/auth/login',
    {
      method: 'OPTIONS',
      headers: { Origin: origin, 'Access-Control-Request-Method': 'POST', 'Access-Control-Request-Headers': 'x-api-key' },
    },
    env,
  )

describe('CORS', () => {
  it.each(['http://localhost:5173', 'https://zonal-ui.pages.dev'])('el preflight de %s pasa sin x-api-key', async (origin) => {
    const res = await preflight(origin)
    expect(res.status).toBe(204)
    expect(res.headers.get('access-control-allow-origin')).toBe(origin)
    expect(res.headers.get('access-control-allow-headers')).toContain('x-api-key')
  })

  it('no autoriza a un origen que no está en UI_ORIGINS', async () => {
    const res = await preflight('https://otro-sitio.com')
    expect(res.headers.get('access-control-allow-origin')).toBeNull()
  })

  it('una petición del navegador sin llave sigue recibiendo 401, con el header de CORS', async () => {
    const res = await app.request('/api/auth/login', { method: 'POST', headers: { Origin: 'http://localhost:5173' } }, env)
    expect(res.status).toBe(401)
    expect(res.headers.get('access-control-allow-origin')).toBe('http://localhost:5173')
  })
})
