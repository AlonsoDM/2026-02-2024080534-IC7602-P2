import { useCallback, useEffect, useState, type FormEvent } from 'react'
import { Link } from 'react-router-dom'
import { verificarDominio } from '../lib/api'
import { useAuth } from '../lib/auth-context'
import { supabase } from '../lib/supabase'

type Dominio = { id: number; name: string; txt_token: string; verified: boolean; verified_at: string | null }

// Mismo formato que acepta la tabla domains (minúsculas, dígitos, guiones y puntos)
const FORMATO_DOMINIO = /^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/

export default function Dominios() {
  const { session } = useAuth()
  const [dominios, setDominios] = useState<Dominio[]>([])
  const [cargando, setCargando] = useState(true)
  const [nombre, setNombre] = useState('')
  const [error, setError] = useState('')
  // Mensaje por dominio tras pedir la verificación, y cuál se está verificando
  const [mensajes, setMensajes] = useState<Record<number, string>>({})
  const [verificando, setVerificando] = useState<number | null>(null)

  const cargar = useCallback(async () => {
    const { data, error } = await supabase
      .from('domains')
      .select('id, name, txt_token, verified, verified_at')
      .order('id')
    if (error) setError('No se pudieron cargar los dominios.')
    else setDominios(data as Dominio[])
    setCargando(false)
  }, [])

  useEffect(() => {
    cargar()
  }, [cargar])

  async function agregar(e: FormEvent) {
    e.preventDefault()
    setError('')
    const limpio = nombre.trim().toLowerCase()
    if (!FORMATO_DOMINIO.test(limpio) || !limpio.includes('.')) {
      setError('Escriba un dominio válido, por ejemplo marca.com.')
      return
    }
    const { error } = await supabase.from('domains').insert({ name: limpio })
    if (error) {
      setError(error.code === '23505' ? 'Ese dominio ya está registrado.' : 'No se pudo agregar el dominio.')
      return
    }
    setNombre('')
    await cargar()
  }

  async function verificar(d: Dominio) {
    setVerificando(d.id)
    setMensajes((m) => ({ ...m, [d.id]: '' }))
    try {
      const { verified } = await verificarDominio(d.id)
      if (verified) await cargar()
      else setMensajes((m) => ({ ...m, [d.id]: 'Todavía no se encuentra el registro TXT. Puede tardar unos minutos en propagarse.' }))
    } catch (err) {
      setMensajes((m) => ({ ...m, [d.id]: err instanceof Error ? err.message : 'No se pudo verificar.' }))
    }
    setVerificando(null)
  }

  async function eliminar(d: Dominio) {
    if (!window.confirm(`¿Eliminar ${d.name}? También se borran sus URLs y credenciales.`)) return
    const { error } = await supabase.from('domains').delete().eq('id', d.id)
    if (error) setError('No se pudo eliminar el dominio.')
    else await cargar()
  }

  return (
    <main className="mx-auto max-w-3xl space-y-6 p-6">
      <header className="flex items-center justify-between">
        <h1 className="text-2xl font-bold text-slate-900">Mis dominios</h1>
        <div className="flex items-center gap-3 text-sm text-slate-600">
          <span>{session?.user.email}</span>
          <button onClick={() => supabase.auth.signOut()} className="rounded border border-slate-300 px-3 py-1">
            Cerrar sesión
          </button>
        </div>
      </header>

      <form onSubmit={agregar} className="flex gap-2">
        <input
          value={nombre}
          onChange={(e) => setNombre(e.target.value)}
          placeholder="marca.com"
          aria-label="Dominio"
          className="flex-1 rounded border border-slate-300 px-3 py-2"
        />
        <button type="submit" className="rounded bg-slate-900 px-4 py-2 text-white">Agregar</button>
      </form>

      {error && <p role="alert" className="text-sm text-red-600">{error}</p>}
      {cargando && <p className="text-slate-500">Cargando…</p>}
      {!cargando && dominios.length === 0 && <p className="text-slate-500">Todavía no tiene dominios.</p>}

      <ul className="space-y-4">
        {dominios.map((d) => (
          <li key={d.id} className="space-y-3 rounded-lg bg-white p-4 shadow">
            <div className="flex items-center justify-between gap-2">
              <span className="font-mono font-semibold">{d.name}</span>
              <span className={d.verified ? 'text-sm text-green-700' : 'text-sm text-amber-700'}>
                {d.verified ? 'Verificado' : 'Pendiente de verificar'}
              </span>
            </div>

            {!d.verified && (
              <div className="space-y-2 text-sm text-slate-700">
                <p>Cree este registro TXT en el DNS de su dominio y luego pulse Verificar:</p>
                <dl className="grid grid-cols-[5rem_1fr] gap-1 rounded bg-slate-100 p-3 font-mono">
                  <dt>Tipo</dt><dd>TXT</dd>
                  <dt>Nombre</dt><dd>{d.name}</dd>
                  <dt>Valor</dt><dd className="break-all">{d.txt_token}</dd>
                </dl>
              </div>
            )}

            {mensajes[d.id] && <p role="status" className="text-sm text-amber-700">{mensajes[d.id]}</p>}

            <div className="flex gap-2">
              {d.verified && (
                <Link to={`/dominios/${d.id}`} className="rounded bg-slate-900 px-3 py-1 text-sm text-white">
                  Administrar URLs
                </Link>
              )}
              {!d.verified && (
                <button
                  onClick={() => verificar(d)}
                  disabled={verificando === d.id}
                  className="rounded bg-slate-900 px-3 py-1 text-sm text-white disabled:opacity-50"
                >
                  {verificando === d.id ? 'Verificando…' : 'Verificar'}
                </button>
              )}
              <button onClick={() => eliminar(d)} className="rounded border border-red-300 px-3 py-1 text-sm text-red-700">
                Eliminar
              </button>
            </div>
          </li>
        ))}
      </ul>
    </main>
  )
}
