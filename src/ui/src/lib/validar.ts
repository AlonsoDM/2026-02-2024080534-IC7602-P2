import type { TipoArchivo } from './tipos'

const FORMATO_PATRON = /^(\*\.)?[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/

// El patrón debe ser el dominio o un subdominio suyo, exacto o con wildcard: "marca.com", "api.marca.com", "*.marca.com".
// Es la misma regla del trigger urls_validar_patron de Supabase; aquí se revisa antes para dar un mensaje claro.
export function validarPatron(patron: string, dominio: string): string | null {
  if (!FORMATO_PATRON.test(patron)) return 'El patrón solo puede llevar minúsculas, números, guiones, puntos y un * al inicio (*.marca.com).'
  const base = patron.replace(/^\*\./, '')
  if (base !== dominio && !base.endsWith('.' + dominio)) return `El patrón debe ser ${dominio} o un subdominio suyo, por ejemplo *.${dominio}.`
  return null
}

export function validarOrigen(origen: string): string | null {
  try {
    const u = new URL(origen)
    if (u.protocol !== 'http:' && u.protocol !== 'https:') return 'El origen debe empezar con http:// o https://.'
    if (!u.hostname) return 'El origen no tiene un servidor.'
  } catch {
    return 'El origen no es una URL válida, por ejemplo https://origen.marca.com.'
  }
  return null
}

// Quita el punto inicial y pasa a minúsculas: ".PNG" queda como "png"
export const normalizarExtension = (e: string) => e.trim().replace(/^\./, '').toLowerCase()

export function validarTipos(tipos: TipoArchivo[]): string | null {
  if (tipos.length === 0) return 'Agregue al menos un tipo de archivo; la caché solo guarda los que aparecen aquí.'
  const vistas = new Set<string>()
  for (const t of tipos) {
    if (!/^[a-z0-9]+$/.test(t.extension)) return `La extensión "${t.extension}" no es válida: use solo letras y números, como png o js.`
    if (vistas.has(t.extension)) return `La extensión ${t.extension} está repetida.`
    vistas.add(t.extension)
    if (!Number.isInteger(t.ttl_seconds) || t.ttl_seconds <= 0) return `El TTL de ${t.extension} debe ser mayor que cero.`
  }
  return null
}
