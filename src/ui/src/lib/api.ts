const API_URL = import.meta.env.VITE_API_URL
const API_KEY = import.meta.env.VITE_API_KEY

export type ResultadoVerificacion = { verified: boolean }

// Pide al Worker que busque el TXT del dominio. Lanza un Error con un mensaje para mostrar si falla
export async function verificarDominio(id: number): Promise<ResultadoVerificacion> {
  let res: Response
  try {
    res = await fetch(`${API_URL}/api/domains/${id}/verify`, { method: 'POST', headers: { 'x-api-key': API_KEY } })
  } catch {
    throw new Error('No se pudo conectar con el servidor. Revise su conexión e intente de nuevo.')
  }
  const cuerpo = (await res.json().catch(() => ({}))) as { verified?: boolean; error?: string }
  if (!res.ok) throw new Error(cuerpo.error ?? `El servidor respondió ${res.status}`)
  return { verified: cuerpo.verified === true }
}


export type ResultadoLogin =
  | { ok: true; sesion: string }
  | { ok: false; motivo: 'credenciales' | 'servidor' | 'red' }

// Inicia sesión en una URL protegida con usuario y contraseña. La sesión no se guarda en la UI:
// solo se le entrega a la caché por el callback.
export async function iniciarSesionUrl(urlId: number, username: string, password: string): Promise<ResultadoLogin> {
  let res: Response
  try {
    res = await fetch(`${API_URL}/api/auth/login`, {
      method: 'POST',
      headers: { 'x-api-key': API_KEY, 'content-type': 'application/json' },
      body: JSON.stringify({ url_id: urlId, username, password }),
    })
  } catch {
    return { ok: false, motivo: 'red' }
  }
  if (res.status === 401) return { ok: false, motivo: 'credenciales' }
  const cuerpo = (await res.json().catch(() => ({}))) as { session?: string }
  if (!res.ok || typeof cuerpo.session !== 'string' || cuerpo.session === '') return { ok: false, motivo: 'servidor' }
  return { ok: true, sesion: cuerpo.session }
}
