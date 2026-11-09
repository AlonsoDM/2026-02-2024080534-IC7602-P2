import { Hono } from 'hono'
import { requireScope } from '../middleware/require-scope'
import type { AppEnv } from '../types'

// Rutas de dominios de la UI, bajo /api/domains. Solo entra la llave con alcance ui
export const domains = new Hono<AppEnv>()

// Para todas las rutas de dominios, se requiere que la llave tenga el scope 'ui'
domains.use('*', requireScope('ui'))

// Resolver DNS sobre HTTPS (DoH) de Cloudflare, con formato JSON
const DOH_URL = 'https://cloudflare-dns.com/dns-query'
const TIPO_TXT = 16

type RespuestaDoh = { Status: number; Answer?: { type: number; data: string }[] }

// Un TXT llega como "valor" entre comillas; si es largo viene en trozos: "abc" "def".
// Quita las comillas y junta los trozos.
export function valorTxt(data: string): string {
  const trozos = data.match(/"((?:[^"\\]|\\.)*)"/g)
  if (!trozos) return data
  return trozos.map((t) => t.slice(1, -1)).join('')
}

// Devuelve los valores de los registros TXT de un nombre. Lanza un error si el resolver falla.
export async function buscarTxt(nombre: string): Promise<string[]> {
  const res = await fetch(`${DOH_URL}?name=${encodeURIComponent(nombre)}&type=TXT`, {
    headers: { accept: 'application/dns-json' },
  })
  if (!res.ok) throw new Error(`El resolver DoH respondió ${res.status}`)

  const cuerpo = (await res.json()) as RespuestaDoh
  // 0 = NOERROR; 3 = NXDOMAIN (el nombre no existe, o sea que no hay TXT). Cualquier otro es una falla
  if (cuerpo.Status !== 0 && cuerpo.Status !== 3) throw new Error(`El resolver DoH devolvió Status ${cuerpo.Status}`)

  return (cuerpo.Answer ?? []).filter((r) => r.type === TIPO_TXT).map((r) => valorTxt(r.data))
}

// POST /:id/verify
// Marca el dominio como verificado solo si su TXT contiene el txt_token guardado en Supabase
domains.post('/:id/verify', async (c) => {
  const id = Number(c.req.param('id'))
  if (!Number.isInteger(id) || id <= 0) {
    return c.json({ error: 'Id de dominio no válido' }, 400)
  }

  const { data: dominio, error } = await c.var.supabase
    .from('domains')
    .select('id, name, txt_token, verified')
    .eq('id', id)
    .maybeSingle()

  if (error) {
    console.error('No se pudo leer el dominio:', error.message)
    return c.json({ error: 'No se pudo leer el dominio' }, 500)
  }
  if (!dominio) {
    return c.json({ error: 'Dominio no encontrado' }, 404)
  }
  if (dominio.verified) {
    return c.json({ verified: true })
  }

  let valores: string[]
  try {
    valores = await buscarTxt(dominio.name)
  } catch (err) {
    console.error('No se pudo consultar el TXT:', err)
    return c.json({ error: 'No se pudo consultar el DNS, intente de nuevo' }, 502)
  }

  // El token lo pone el servidor, nunca se lee del cliente
  if (!valores.some((v) => v.trim() === dominio.txt_token)) {
    return c.json({ verified: false })
  }

  const { error: errorUpdate } = await c.var.supabase
    .from('domains')
    .update({ verified: true, verified_at: new Date().toISOString() })
    .eq('id', id)

  if (errorUpdate) {
    console.error('No se pudo marcar el dominio como verificado:', errorUpdate.message)
    return c.json({ error: 'No se pudo guardar la verificación' }, 500)
  }
  return c.json({ verified: true })
})
