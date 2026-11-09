import { useState, type FormEvent } from 'react'
import { bytesAMb, dividirTtl, mbABytes, UNIDADES_TTL } from '../lib/formato'
import { supabase } from '../lib/supabase'
import { AUTENTICACIONES, POLITICAS, type Autenticacion, type Politica, type UrlConfig } from '../lib/tipos'
import { normalizarExtension, validarOrigen, validarPatron, validarTipos } from '../lib/validar'

type FilaTipo = { extension: string; cantidad: string; segundos: number }

type Props = {
  dominio: { id: number; name: string }
  // Si viene, se edita esa URL; si no, se crea una nueva
  inicial?: UrlConfig
  onGuardado: () => void
  onCancelar: () => void
}

const TIPOS_SUGERIDOS: FilaTipo[] = [
  { extension: 'html', cantidad: '5', segundos: 60 },
  { extension: 'css', cantidad: '1', segundos: 3600 },
  { extension: 'js', cantidad: '1', segundos: 3600 },
  { extension: 'png', cantidad: '1', segundos: 86400 },
]

// Crear o modificar una URL con su origen, tamaño, política, tipos de archivo con su TTL y tipo de autenticación
export default function UrlForm({ dominio, inicial, onGuardado, onCancelar }: Props) {
  const [patron, setPatron] = useState(inicial?.pattern ?? '')
  const [origen, setOrigen] = useState(inicial?.origin ?? '')
  const [tamano, setTamano] = useState(inicial ? String(bytesAMb(inicial.cache_size_bytes)) : '10')
  const [politica, setPolitica] = useState<Politica>(inicial?.replacement_policy ?? 'lru')
  const [auth, setAuth] = useState<Autenticacion>(inicial?.auth ?? 'none')
  const [tipos, setTipos] = useState<FilaTipo[]>(
    inicial
      ? [...inicial.url_file_types]
          .sort((a, b) => a.extension.localeCompare(b.extension))
          .map((t) => {
            const { cantidad, segundos } = dividirTtl(t.ttl_seconds)
            return { extension: t.extension, cantidad: String(cantidad), segundos }
          })
      : TIPOS_SUGERIDOS,
  )
  const [error, setError] = useState('')
  const [guardando, setGuardando] = useState(false)

  const cambiarTipo = (i: number, cambios: Partial<FilaTipo>) =>
    setTipos((ts) => ts.map((t, j) => (j === i ? { ...t, ...cambios } : t)))

  async function guardar(e: FormEvent) {
    e.preventDefault()
    setError('')

    const patronLimpio = patron.trim().toLowerCase()
    const origenLimpio = origen.trim().replace(/\/+$/, '')
    const mb = Number(tamano)
    const tiposFinales = tipos.map((t) => ({
      extension: normalizarExtension(t.extension),
      ttl_seconds: Math.round(Number(t.cantidad) * t.segundos),
    }))

    const problema =
      validarPatron(patronLimpio, dominio.name) ??
      validarOrigen(origenLimpio) ??
      (!(mb > 0) ? 'El tamaño de la caché debe ser mayor que cero.' : null) ??
      validarTipos(tiposFinales)
    if (problema) {
      setError(problema)
      return
    }

    setGuardando(true)
    const fila = {
      pattern: patronLimpio,
      origin: origenLimpio,
      cache_size_bytes: mbABytes(mb),
      replacement_policy: politica,
      auth,
    }

    let urlId = inicial?.id
    if (inicial) {
      const { error } = await supabase.from('urls').update(fila).eq('id', inicial.id)
      if (error) return fallo(error.code)
    } else {
      const { data, error } = await supabase
        .from('urls')
        .insert({ ...fila, domain_id: dominio.id })
        .select('id')
        .single()
      if (error || !data) return fallo(error?.code)
      urlId = data.id as number
    }

    // Los tipos se reemplazan completos: se borran los viejos y se guardan los de la pantalla
    const { error: errorBorrar } = await supabase.from('url_file_types').delete().eq('url_id', urlId)
    const { error: errorTipos } = errorBorrar
      ? { error: errorBorrar }
      : await supabase.from('url_file_types').insert(tiposFinales.map((t) => ({ url_id: urlId, ...t })))
    if (errorTipos) {
      // Una URL nueva sin tipos no serviría de nada: se deshace para que se pueda volver a intentar
      if (!inicial) await supabase.from('urls').delete().eq('id', urlId)
      setError('No se pudieron guardar los tipos de archivo. Intente de nuevo.')
      setGuardando(false)
      return
    }
    setGuardando(false)
    onGuardado()
  }

  function fallo(codigo?: string) {
    setError(
      codigo === '23505'
        ? 'Ya existe una URL con ese patrón.'
        : codigo === '42501'
          ? 'No tiene permiso para guardar esta URL.'
          : 'No se pudo guardar la URL. Revise los datos e intente de nuevo.',
    )
    setGuardando(false)
  }

  const campo = 'mt-1 w-full rounded border border-slate-300 px-3 py-2'

  return (
    <form onSubmit={guardar} className="space-y-4 rounded-lg bg-white p-5 shadow">
      <h2 className="text-lg font-semibold text-slate-900">{inicial ? 'Modificar URL' : 'Nueva URL'}</h2>

      <label className="block text-sm text-slate-700">
        Patrón
        <input value={patron} onChange={(e) => setPatron(e.target.value)} placeholder={`*.${dominio.name}`} className={campo} />
        <span className="text-xs text-slate-500">
          Un nombre exacto ({dominio.name}, www.{dominio.name}) o con wildcard (*.{dominio.name}). La exacta gana a la wildcard.
        </span>
      </label>

      <label className="block text-sm text-slate-700">
        Origen
        <input value={origen} onChange={(e) => setOrigen(e.target.value)} placeholder="https://origen.ejemplo.com" className={campo} />
        <span className="text-xs text-slate-500">El servidor al que la caché pide el recurso cuando no lo tiene.</span>
      </label>

      <div className="grid gap-4 sm:grid-cols-2">
        <label className="block text-sm text-slate-700">
          Tamaño máximo de la caché (MB)
          <input type="number" min="0" step="any" value={tamano} onChange={(e) => setTamano(e.target.value)} className={campo} />
        </label>
        <label className="block text-sm text-slate-700">
          Política de reemplazo
          <select value={politica} onChange={(e) => setPolitica(e.target.value as Politica)} className={campo}>
            {POLITICAS.map((p) => <option key={p.valor} value={p.valor}>{p.nombre}</option>)}
          </select>
        </label>
      </div>

      <label className="block text-sm text-slate-700">
        Autenticación
        <select value={auth} onChange={(e) => setAuth(e.target.value as Autenticacion)} className={campo}>
          {AUTENTICACIONES.map((a) => <option key={a.valor} value={a.valor}>{a.nombre}</option>)}
        </select>
        {inicial && inicial.auth !== auth && (
          <span className="text-xs text-amber-700">
            Las credenciales del tipo anterior no se borran, pero la caché solo usa las del tipo elegido.
          </span>
        )}
      </label>

      <fieldset className="space-y-2">
        <legend className="text-sm text-slate-700">Tipos de archivo que se guardan y su TTL</legend>
        {tipos.map((t, i) => (
          <div key={i} className="flex flex-wrap items-center gap-2">
            <input
              value={t.extension}
              onChange={(e) => cambiarTipo(i, { extension: e.target.value })}
              placeholder="png"
              aria-label="Extensión"
              className="w-24 rounded border border-slate-300 px-3 py-2"
            />
            <input
              type="number"
              min="0"
              step="any"
              value={t.cantidad}
              onChange={(e) => cambiarTipo(i, { cantidad: e.target.value })}
              aria-label="TTL"
              className="w-24 rounded border border-slate-300 px-3 py-2"
            />
            <select
              value={t.segundos}
              onChange={(e) => cambiarTipo(i, { segundos: Number(e.target.value) })}
              aria-label="Unidad del TTL"
              className="rounded border border-slate-300 px-3 py-2"
            >
              {UNIDADES_TTL.map((u) => <option key={u.segundos} value={u.segundos}>{u.nombre}</option>)}
            </select>
            <button
              type="button"
              onClick={() => setTipos((ts) => ts.filter((_, j) => j !== i))}
              className="rounded border border-slate-300 px-2 py-1 text-sm text-slate-600"
              aria-label={`Quitar ${t.extension || 'tipo'}`}
            >
              Quitar
            </button>
          </div>
        ))}
        <button
          type="button"
          onClick={() => setTipos((ts) => [...ts, { extension: '', cantidad: '1', segundos: 3600 }])}
          className="rounded border border-slate-300 px-3 py-1 text-sm text-slate-700"
        >
          Agregar tipo
        </button>
      </fieldset>

      {error && <p role="alert" className="text-sm text-red-600">{error}</p>}

      <div className="flex gap-2">
        <button type="submit" disabled={guardando} className="rounded bg-slate-900 px-4 py-2 text-white disabled:opacity-50">
          {guardando ? 'Guardando…' : 'Guardar'}
        </button>
        <button type="button" onClick={onCancelar} className="rounded border border-slate-300 px-4 py-2">
          Cancelar
        </button>
      </div>
    </form>
  )
}
