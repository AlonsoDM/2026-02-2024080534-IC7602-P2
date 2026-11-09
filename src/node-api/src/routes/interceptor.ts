import { Hono } from 'hono'
import { distinctRemoteAddr } from 'hono/utils/ipaddr'
import { requireScope } from '../middleware/require-scope'
import type { AppEnv } from '../types'

// Rutas del DNS Interceptor, bajo /api/interceptor. Solo entra la llave con alcance interceptor
export const interceptor = new Hono<AppEnv>()

// Para todas las rutas de interceptor se requiere que la llave tenga el scope 'interceptor'
interceptor.use('*', requireScope('interceptor'))

interceptor.get('/resolve', async (c) => {
  const domain = c.req.query('domain')?.toLowerCase().replace(/\.$/, '')
  const clientIp = c.req.query('client_ip')
  if (!domain || domain !== domain.trim() || domain.length > 253 || !domain.split('.').every((label) =>
    /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(label),
  ) || !clientIp || clientIp !== clientIp.trim() || distinctRemoteAddr(clientIp) !== 'IPv4') {
    return c.json({ error: 'Se requieren domain y client_ip IPv4 válidos' }, 400)
  }

  const { data: urls, error: urlsError } = await c.var.supabase
    .from('urls')
    .select('pattern, domains!inner (verified)')
    .eq('domains.verified', true)
  if (urlsError) {
    console.error('No se pudieron leer las URLs:', urlsError.message)
    return c.json({ error: 'No se pudieron leer las URLs' }, 500)
  }
  const exists = (urls as unknown as { pattern: string }[]).some(({ pattern }) => {
    if (!pattern.startsWith('*.')) return domain === pattern
    const base = pattern.slice(2)
    return domain === base || domain.endsWith(`.${base}`)
  })
  if (!exists) return c.json({ exists: false })

  const { data: country, error: countryError } = await c.var.supabase
    .rpc('pais_de_ip', { p_ip: clientIp })
  if (countryError) {
    console.error('No se pudo obtener el país:', countryError.message)
    return c.json({ error: 'No se pudo obtener el país' }, 500)
  }
  if (!country || country === 'ZZ') {
    return c.json({ error: 'No se conoce el país del cliente' }, 500)
  }

  const { data: zones, error: zonesError } = await c.var.supabase
    .from('zones')
    .select('code, ip')
    .contains('countries', [country])
    .limit(1)
  if (zonesError) {
    console.error('No se pudieron leer las zonas:', zonesError.message)
    return c.json({ error: 'No se pudieron leer las zonas' }, 500)
  }
  const zone = zones?.[0]
  if (!zone?.code || !zone.ip || distinctRemoteAddr(zone.ip) !== 'IPv4') {
    return c.json({ error: 'No hay una caché IPv4 para el país del cliente' }, 500)
  }
  return c.json({ exists: true, zone: zone.code, address: zone.ip })
})
