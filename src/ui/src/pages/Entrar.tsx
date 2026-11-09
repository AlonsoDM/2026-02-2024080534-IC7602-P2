import { useState, type FormEvent } from 'react'
import { Navigate } from 'react-router-dom'
import { useAuth } from '../lib/auth-context'
import { supabase } from '../lib/supabase'

// Crear cuenta e iniciar sesión con Supabase Auth
export default function Entrar() {
  const { session } = useAuth()
  const [modo, setModo] = useState<'entrar' | 'registro'>('entrar')
  const [correo, setCorreo] = useState('')
  const [clave, setClave] = useState('')
  const [error, setError] = useState('')
  const [aviso, setAviso] = useState('')
  const [enviando, setEnviando] = useState(false)

  if (session) return <Navigate to="/" replace />

  async function enviar(e: FormEvent) {
    e.preventDefault()
    setError('')
    setAviso('')
    setEnviando(true)
    if (modo === 'entrar') {
      const { error } = await supabase.auth.signInWithPassword({ email: correo, password: clave })
      if (error) setError('Correo o contraseña incorrectos.')
    } else {
      const { data, error } = await supabase.auth.signUp({ email: correo, password: clave })
      if (error) setError(error.message)
      // Con la confirmación por correo activa no hay sesión hasta que la persona abre el enlace
      else if (!data.session) setAviso('Cuenta creada. Revise su correo y abra el enlace de confirmación para entrar.')
    }
    setEnviando(false)
  }

  return (
    <main className="flex min-h-screen items-center justify-center bg-slate-50 p-4">
      <form onSubmit={enviar} className="w-full max-w-sm space-y-4 rounded-lg bg-white p-6 shadow">
        <h1 className="text-xl font-bold text-slate-900">{modo === 'entrar' ? 'Iniciar sesión' : 'Crear cuenta'}</h1>

        <label className="block text-sm text-slate-700">
          Correo
          <input
            type="email"
            required
            autoComplete="email"
            value={correo}
            onChange={(e) => setCorreo(e.target.value)}
            className="mt-1 w-full rounded border border-slate-300 px-3 py-2"
          />
        </label>
        <label className="block text-sm text-slate-700">
          Contraseña
          <input
            type="password"
            required
            minLength={6}
            autoComplete={modo === 'entrar' ? 'current-password' : 'new-password'}
            value={clave}
            onChange={(e) => setClave(e.target.value)}
            className="mt-1 w-full rounded border border-slate-300 px-3 py-2"
          />
        </label>

        {error && <p role="alert" className="text-sm text-red-600">{error}</p>}
        {aviso && <p role="status" className="text-sm text-green-700">{aviso}</p>}

        <button
          type="submit"
          disabled={enviando}
          className="w-full rounded bg-slate-900 px-3 py-2 text-white disabled:opacity-50"
        >
          {enviando ? 'Enviando…' : modo === 'entrar' ? 'Entrar' : 'Crear cuenta'}
        </button>

        <button
          type="button"
          onClick={() => {
            setModo(modo === 'entrar' ? 'registro' : 'entrar')
            setError('')
            setAviso('')
          }}
          className="w-full text-sm text-slate-600 underline"
        >
          {modo === 'entrar' ? 'No tengo cuenta: crear una' : 'Ya tengo cuenta: iniciar sesión'}
        </button>
      </form>
    </main>
  )
}
