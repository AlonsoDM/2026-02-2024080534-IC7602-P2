import type { ReactNode } from 'react'
import { Navigate } from 'react-router-dom'
import { useAuth } from '../lib/auth-context'

// Solo deja pasar a quien tiene sesión; a las demás personas las manda a /entrar
export default function Ruta({ children }: { children: ReactNode }) {
  const { session, cargando } = useAuth()
  if (cargando) return <p className="p-8 text-slate-500">Cargando…</p>
  if (!session) return <Navigate to="/entrar" replace />
  return children
}
