import { useEffect, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import ApiKeys from '../components/ApiKeys'
import UrlForm from '../components/UrlForm'
import UsuariosUrl from '../components/UsuariosUrl'
import { formatearBytes, formatearTtl } from '../lib/formato'
import { supabase } from '../lib/supabase'
import { AUTENTICACIONES, POLITICAS, type UrlConfig } from '../lib/tipos'

type Dominio = { id: number; name: string; verified: boolean }

// URLs de un dominio verificado: crear, modificar y eliminar, con sus credenciales según el tipo de autenticación
export default function Urls() {
  const id = Number(useParams().id)
  const [dominio, setDominio] = useState<Dominio | null>(null)
  const [urls, setUrls] = useState<UrlConfig[]>([])
  const [estado, setEstado] = useState<'cargando' | 'listo' | 'noexiste'>('cargando')
  // undefined = formulario cerrado, null = nueva URL, UrlConfig = modificar esa
  const [formulario, setFormulario] = useState<UrlConfig | null | undefined>(undefined)
  const [error, setError] = useState('')

  // Cada cambio de este número vuelve a leer el dominio y sus URLs
  const [version, setVersion] = useState(0)
  const recargar = () => setVersion((v) => v + 1)

  useEffect(() => {
    let activo = true
    async function leer() {
      const { data: d } = await supabase.from('domains').select('id, name, verified').eq('id', id).maybeSingle()
      if (!activo) return
      if (!d) {
        setEstado('noexiste')
        return
      }
      setDominio(d as Dominio)
      const { data, error } = await supabase
        .from('urls')
        .select('id, domain_id, pattern, origin, cache_size_bytes, replacement_policy, auth, url_file_types(extension, ttl_seconds)')
        .eq('domain_id', id)
        .order('id')
      if (!activo) return
      if (error) setError('No se pudieron cargar las URLs.')
      else setUrls(data as unknown as UrlConfig[])
      setEstado('listo')
    }
    leer()
    return () => {
      activo = false
    }
  }, [id, version])

  async function eliminar(u: UrlConfig) {
    if (!window.confirm(`¿Eliminar ${u.pattern}? También se borran sus tipos de archivo y credenciales.`)) return
    const { error } = await supabase.from('urls').delete().eq('id', u.id)
    if (error) setError('No se pudo eliminar la URL.')
    else recargar()
  }

  if (estado === 'cargando') return <p className="p-8 text-slate-500">Cargando…</p>
  if (estado === 'noexiste' || !dominio) {
    return (
      <main className="mx-auto max-w-3xl space-y-4 p-6">
        <p className="text-slate-700">No se encontró ese dominio.</p>
        <Link to="/" className="underline">Volver a mis dominios</Link>
      </main>
    )
  }
  if (!dominio.verified) {
    return (
      <main className="mx-auto max-w-3xl space-y-4 p-6">
        <p className="text-slate-700">Verifique {dominio.name} con su registro TXT antes de crear URLs.</p>
        <Link to="/" className="underline">Volver a mis dominios</Link>
      </main>
    )
  }

  return (
    <main className="mx-auto max-w-3xl space-y-6 p-6">
      <header className="space-y-1">
        <Link to="/" className="text-sm text-slate-600 underline">← Mis dominios</Link>
        <h1 className="text-2xl font-bold text-slate-900">URLs de <span className="font-mono">{dominio.name}</span></h1>
      </header>

      {formulario === undefined && (
        <button onClick={() => setFormulario(null)} className="rounded bg-slate-900 px-4 py-2 text-white">Nueva URL</button>
      )}

      {formulario !== undefined && (
        <UrlForm
          // Cambiar de URL reinicia el formulario con sus datos
          key={formulario?.id ?? 'nueva'}
          dominio={dominio}
          inicial={formulario ?? undefined}
          onGuardado={() => {
            setFormulario(undefined)
            recargar()
          }}
          onCancelar={() => setFormulario(undefined)}
        />
      )}

      {error && <p role="alert" className="text-sm text-red-600">{error}</p>}
      {urls.length === 0 && formulario === undefined && <p className="text-slate-500">Este dominio todavía no tiene URLs.</p>}

      <ul className="space-y-4">
        {urls.map((u) => (
          <li key={u.id} className="space-y-3 rounded-lg bg-white p-4 shadow">
            <div className="flex flex-wrap items-center justify-between gap-2">
              <span className="font-mono font-semibold">{u.pattern}</span>
              <span className="flex gap-2">
                <button onClick={() => setFormulario(u)} className="rounded border border-slate-300 px-3 py-1 text-sm">Modificar</button>
                <button onClick={() => eliminar(u)} className="rounded border border-red-300 px-3 py-1 text-sm text-red-700">Eliminar</button>
              </span>
            </div>

            <dl className="grid grid-cols-[8rem_1fr] gap-1 text-sm text-slate-700">
              <dt className="text-slate-500">Origen</dt><dd className="break-all font-mono">{u.origin}</dd>
              <dt className="text-slate-500">Tamaño máximo</dt><dd>{formatearBytes(u.cache_size_bytes)}</dd>
              <dt className="text-slate-500">Reemplazo</dt>
              <dd>{POLITICAS.find((p) => p.valor === u.replacement_policy)?.nombre}</dd>
              <dt className="text-slate-500">Autenticación</dt>
              <dd>{AUTENTICACIONES.find((a) => a.valor === u.auth)?.nombre}</dd>
              <dt className="text-slate-500">Tipos y TTL</dt>
              <dd>
                {[...u.url_file_types]
                  .sort((a, b) => a.extension.localeCompare(b.extension))
                  .map((t) => `${t.extension}: ${formatearTtl(t.ttl_seconds)}`)
                  .join(' · ') || 'ninguno'}
              </dd>
            </dl>

            {u.auth === 'api_key' && <ApiKeys urlId={u.id} />}
            {u.auth === 'user_password' && <UsuariosUrl urlId={u.id} />}
          </li>
        ))}
      </ul>
    </main>
  )
}
