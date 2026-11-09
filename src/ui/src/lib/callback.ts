// La caché manda a la persona a /login?url=<id de la URL>&callback=<esquema>://<host>/__auth/callback?next=<ruta>
// (ver docs/03-arquitectura/zonal-cache.md). El callback viene en el enlace, así que cualquiera lo puede escribir:
// antes de mandarle la sesión se revisa que tenga la forma que arma la caché.

export type ParametrosLogin =
  | { ok: true; urlId: number; callback: URL }
  | { ok: false; motivo: string }

const RUTA_CALLBACK = '/__auth/callback'

export function leerParametros(busqueda: string): ParametrosLogin {
  const params = new URLSearchParams(busqueda)

  const url = params.get('url') ?? ''
  const urlId = Number(url)
  if (!/^\d+$/.test(url) || !Number.isSafeInteger(urlId) || urlId <= 0) {
    return { ok: false, motivo: 'Al enlace le falta indicar a qué sitio se quiere entrar.' }
  }

  const crudo = params.get('callback')
  if (!crudo) return { ok: false, motivo: 'Al enlace le falta la dirección a la que volver después de entrar.' }

  let callback: URL
  try {
    callback = new URL(crudo)
  } catch {
    return { ok: false, motivo: 'La dirección a la que volver no es válida.' }
  }
  if (callback.protocol !== 'http:' && callback.protocol !== 'https:') {
    return { ok: false, motivo: 'La dirección a la que volver no es válida.' }
  }
  // Una dirección como https://sitio.com@otro.com engaña a quien la lee: el sitio real sería otro.com
  if (callback.username || callback.password) {
    return { ok: false, motivo: 'La dirección a la que volver no es válida.' }
  }
  // Solo se vuelve a la ruta de callback de una caché, nunca a una página cualquiera
  if (callback.pathname !== RUTA_CALLBACK) {
    return { ok: false, motivo: 'La dirección a la que volver no es la de una caché del sistema.' }
  }
  // La sesión viaja en la dirección: no puede haber ya una (la agrega la UI) ni un fragmento que la oculte
  if (callback.searchParams.has('session') || callback.hash) {
    return { ok: false, motivo: 'La dirección a la que volver no es válida.' }
  }
  return { ok: true, urlId, callback }
}

// callback + &session=<sesión>. El callback ya trae "?next=…", pero se cubre también que no lo traiga
export function destinoConSesion(callback: URL, sesion: string): string {
  return callback.toString() + (callback.search ? '&' : '?') + 'session=' + encodeURIComponent(sesion)
}
