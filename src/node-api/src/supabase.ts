import { createClient, type SupabaseClient } from '@supabase/supabase-js'
import type { Bindings } from './types'

export type CreateSupabase = (env: Bindings) => SupabaseClient

// Usa la secret key, que se salta RLS: este cliente solo existe dentro del Worker.
export const createSupabase: CreateSupabase = (env) => {
  if (!env.SUPABASE_URL || !env.SUPABASE_SECRET_KEY) {
    throw new Error('Faltan SUPABASE_URL o SUPABASE_SECRET_KEY en el entorno')
  }
  return createClient(env.SUPABASE_URL, env.SUPABASE_SECRET_KEY, {
    // El Worker no tiene sesión de usuario: cada petición es independiente.
    auth: { persistSession: false, autoRefreshToken: false },
  })
}
