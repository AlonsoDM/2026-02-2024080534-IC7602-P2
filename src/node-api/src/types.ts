import type { SupabaseClient } from '@supabase/supabase-js'

// Variables del entorno: .dev.vars en local y secrets para Cloudflare
export type Bindings = {
  SUPABASE_URL: string
  SUPABASE_SECRET_KEY: string
  // Orígenes de la UI que pueden llamar al Worker desde el navegador (ver wrangler.jsonc)
  UI_ORIGINS?: string
  // Segundos que dura una sesión de /api/auth/login (ver wrangler.jsonc)
  SESSION_TTL_SECONDS?: string
}

// Scopes posibles de un key, igual que el enum component_scope de Supabase
export type Scope = 'ui' | 'interceptor' | 'cache'

// Variables que se pasan entre middlewares y handlers de Hono
export type Variables = {
  supabase: SupabaseClient
  scope: Scope
}

export type AppEnv = { Bindings: Bindings; Variables: Variables }