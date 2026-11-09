import type { Session } from '@supabase/supabase-js'
import { createContext, useContext } from 'react'

export type AuthState = {
  session: Session | null
  // true mientras se lee la sesión guardada; evita mandar a /entrar a quien ya inició sesión
  cargando: boolean
}

export const AuthContext = createContext<AuthState>({ session: null, cargando: true })

export const useAuth = () => useContext(AuthContext)
