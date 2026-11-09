const MB = 1024 * 1024

export const mbABytes = (mb: number) => Math.round(mb * MB)
export const bytesAMb = (bytes: number) => Number((bytes / MB).toFixed(2))

export function formatearBytes(bytes: number): string {
  if (bytes >= MB) return `${bytesAMb(bytes)} MB`
  if (bytes >= 1024) return `${Number((bytes / 1024).toFixed(1))} KB`
  return `${bytes} B`
}

// El TTL se guarda en segundos, pero se edita con una unidad cómoda
export const UNIDADES_TTL = [
  { nombre: 'segundos', segundos: 1 },
  { nombre: 'minutos', segundos: 60 },
  { nombre: 'horas', segundos: 3600 },
  { nombre: 'días', segundos: 86400 },
] as const

// La unidad más grande que divide exactamente el TTL: 7200 s se ve como 2 horas
export function dividirTtl(ttlSegundos: number): { cantidad: number; segundos: number } {
  for (const u of [...UNIDADES_TTL].reverse()) {
    if (ttlSegundos % u.segundos === 0) return { cantidad: ttlSegundos / u.segundos, segundos: u.segundos }
  }
  return { cantidad: ttlSegundos, segundos: 1 }
}

export function formatearTtl(ttlSegundos: number): string {
  const { cantidad, segundos } = dividirTtl(ttlSegundos)
  const nombre = UNIDADES_TTL.find((u) => u.segundos === segundos)?.nombre ?? 'segundos'
  // Singular con 1: "1 hora" y "1 día", no "1 horas"
  const unidad = cantidad === 1 ? (nombre === 'días' ? 'día' : nombre.slice(0, -1)) : nombre
  return `${cantidad} ${unidad}`
}
