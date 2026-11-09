// SHA-256 en hexadecimal, igual que sha256Hex del Worker: el Worker compara contra este hash
export async function sha256Hex(texto: string): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(texto))
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('')
}

// Llave aleatoria de 256 bits con un prefijo que la hace reconocible: zc_ + 64 caracteres hexadecimales.
// Solo se muestra una vez; en Supabase queda su hash y los primeros caracteres.
export function generarLlave(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32))
  return 'zc_' + [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('')
}

export const PREFIJO_LARGO = 8
