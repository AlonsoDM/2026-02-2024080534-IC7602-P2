import { useState, type FormEvent } from 'react'
import { useLocation } from 'react-router-dom'
import { iniciarSesionUrl } from '../lib/api'
import { destinoConSesion, leerParametros } from '../lib/callback'

const MENSAJES = {
  credenciales: 'Usuario o contraseña incorrectos.',
  servidor: 'El servidor no pudo iniciar la sesión. Intente de nuevo en un momento.',
  red: 'No se pudo conectar con el servidor. Revise su conexión e intente de nuevo.',
} as const

// Formulario al que la caché manda con 302 cuando una URL pide usuario y contraseña (T-14).
// No usa la cuenta de Supabase de quien administra: las credenciales son las de esa URL.
export default function Login() {
  const parametros = leerParametros(useLocation().search)
  const [usuario, setUsuario] = useState('')
  const [clave, setClave] = useState('')
  const [error, setError] = useState('')
  const [enviando, setEnviando] = useState(false)

  if (!parametros.ok) {
    return (
      <main className="flex min-h-screen items-center justify-center bg-slate-50 p-4">
        <div className="w-full max-w-sm space-y-3 rounded-lg bg-white p-6 shadow">
          <h1 className="text-xl font-bold text-slate-900">Enlace no válido</h1>
          <p role="alert" className="text-sm text-slate-700">{parametros.motivo}</p>
          <p className="text-sm text-slate-500">Vuelva a abrir el sitio que quería visitar para que le llegue un enlace nuevo.</p>
        </div>
      </main>
    )
  }

  const { urlId, callback } = parametros

  async function enviar(e: FormEvent) {
    e.preventDefault()
    setError('')
    setEnviando(true)
    const r = await iniciarSesionUrl(urlId, usuario, clave)
    if (!r.ok) {
      setError(MENSAJES[r.motivo])
      setClave('')
      setEnviando(false)
      return
    }
    // Sin history: el botón "atrás" no debe volver a un formulario que ya cumplió
    window.location.replace(destinoConSesion(callback, r.sesion))
  }

  return (
    <main className="flex min-h-screen items-center justify-center bg-slate-50 p-4">
      <form onSubmit={enviar} className="w-full max-w-sm space-y-4 rounded-lg bg-white p-6 shadow">
        <div className="space-y-1">
          <h1 className="text-xl font-bold text-slate-900">Iniciar sesión</h1>
          <p className="text-sm text-slate-600">
            Para entrar a <span className="break-all font-mono font-semibold">{callback.host}</span>
          </p>
        </div>

        <label className="block text-sm text-slate-700">
          Usuario
          <input
            required
            autoFocus
            autoComplete="username"
            value={usuario}
            onChange={(e) => setUsuario(e.target.value)}
            className="mt-1 w-full rounded border border-slate-300 px-3 py-2"
          />
        </label>
        <label className="block text-sm text-slate-700">
          Contraseña
          <input
            type="password"
            required
            autoComplete="current-password"
            value={clave}
            onChange={(e) => setClave(e.target.value)}
            className="mt-1 w-full rounded border border-slate-300 px-3 py-2"
          />
        </label>

        {error && <p role="alert" className="text-sm text-red-600">{error}</p>}

        <button
          type="submit"
          disabled={enviando}
          className="w-full rounded bg-slate-900 px-3 py-2 text-white disabled:opacity-50"
        >
          {enviando ? 'Entrando…' : 'Entrar'}
        </button>
      </form>
    </main>
  )
}
