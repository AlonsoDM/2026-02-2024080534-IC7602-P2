// Mismos valores que los enums replacement_policy y auth_type de Supabase
export type Politica = 'lru' | 'lfu' | 'fifo' | 'mru' | 'random'
export type Autenticacion = 'none' | 'api_key' | 'user_password'

export const POLITICAS: { valor: Politica; nombre: string }[] = [
  { valor: 'lru', nombre: 'LRU (la menos usada recientemente)' },
  { valor: 'lfu', nombre: 'LFU (la menos usada con frecuencia)' },
  { valor: 'fifo', nombre: 'FIFO (la más antigua)' },
  { valor: 'mru', nombre: 'MRU (la más usada recientemente)' },
  { valor: 'random', nombre: 'Aleatoria' },
]

export const AUTENTICACIONES: { valor: Autenticacion; nombre: string }[] = [
  { valor: 'none', nombre: 'Sin autenticación' },
  { valor: 'api_key', nombre: 'API Key (header x-api-key)' },
  { valor: 'user_password', nombre: 'Usuario y contraseña' },
]

export type TipoArchivo = { extension: string; ttl_seconds: number }

export type UrlConfig = {
  id: number
  domain_id: number
  pattern: string
  origin: string
  cache_size_bytes: number
  replacement_policy: Politica
  auth: Autenticacion
  url_file_types: TipoArchivo[]
}
