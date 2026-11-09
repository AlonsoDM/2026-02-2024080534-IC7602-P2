import { useEffect, useState, type FormEvent } from 'react'
import { generarLlave, PREFIJO_LARGO, sha256Hex } from '../lib/llaves'
import { supabase } from '../lib/supabase'

type ApiKey = { id: number; label: string; prefix: string; created_at: string }

// API Keys de una URL con auth = api_key. La llave se genera aquí, se muestra una sola vez y en Supabase queda su hash
export default function ApiKeys({ urlId }: { urlId: number }) {
  const [llaves, setLlaves] = useState<ApiKey[]>([])
  const [etiqueta, setEtiqueta] = useState('')
  const [nueva, setNueva] = useState<string | null>(null)
  const [copiada, setCopiada] = useState(false)
  const [error, setError] = useState('')

  // Cada cambio de este número vuelve a leer la lista
  const [version, setVersion] = useState(0)
  const recargar = () => setVersion((v) => v + 1)

  useEffect(() => {
    let activo = true
    supabase
      .from('api_keys')
      .select('id, label, prefix, created_at')
      .eq('url_id', urlId)
      .order('id')
      .then(({ data, error }) => {
        if (!activo) return
        if (error) setError('No se pudieron cargar las API Keys.')
        else setLlaves(data as ApiKey[])
      })
    return () => {
      activo = false
    }
  }, [urlId, version])

  async function crear(e: FormEvent) {
    e.preventDefault()
    setError('')
    const llave = generarLlave()
    const { error } = await supabase.from('api_keys').insert({
      url_id: urlId,
      label: etiqueta.trim(),
      prefix: llave.slice(0, PREFIJO_LARGO),
      key_hash: await sha256Hex(llave),
    })
    if (error) {
      setError('No se pudo crear la API Key.')
      return
    }
    setNueva(llave)
    setCopiada(false)
    setEtiqueta('')
    recargar()
  }

  async function eliminar(k: ApiKey) {
    if (!window.confirm(`¿Eliminar la API Key ${k.prefix}…? Quien la use dejará de poder entrar.`)) return
    const { error } = await supabase.from('api_keys').delete().eq('id', k.id)
    if (error) setError('No se pudo eliminar la API Key.')
    else recargar()
  }

  async function copiar() {
    if (!nueva) return
    try {
      await navigator.clipboard.writeText(nueva)
      setCopiada(true)
    } catch {
      setCopiada(false)
    }
  }

  return (
    <section className="space-y-3 rounded bg-slate-50 p-3" aria-label="API Keys">
      <h3 className="text-sm font-semibold text-slate-800">API Keys</h3>

      {nueva && (
        <div role="status" className="space-y-2 rounded border border-amber-300 bg-amber-50 p-3 text-sm">
          <p className="font-medium text-amber-900">Copie la llave ahora: no se vuelve a mostrar.</p>
          <code className="block break-all rounded bg-white p-2 font-mono">{nueva}</code>
          <div className="flex gap-2">
            <button type="button" onClick={copiar} className="rounded border border-slate-300 bg-white px-3 py-1">
              {copiada ? 'Copiada' : 'Copiar'}
            </button>
            <button type="button" onClick={() => setNueva(null)} className="rounded border border-slate-300 bg-white px-3 py-1">
              Ya la guardé
            </button>
          </div>
        </div>
      )}

      {llaves.length === 0 ? (
        <p className="text-sm text-slate-500">Esta URL todavía no tiene API Keys: nadie puede entrar.</p>
      ) : (
        <ul className="divide-y divide-slate-200 text-sm">
          {llaves.map((k) => (
            <li key={k.id} className="flex items-center justify-between gap-2 py-2">
              <span>
                <span className="font-mono">{k.prefix}…</span>
                {k.label && <span className="ml-2 text-slate-600">{k.label}</span>}
                <span className="ml-2 text-xs text-slate-500">{new Date(k.created_at).toLocaleDateString()}</span>
              </span>
              <button onClick={() => eliminar(k)} className="rounded border border-red-300 px-2 py-1 text-red-700">
                Eliminar
              </button>
            </li>
          ))}
        </ul>
      )}

      <form onSubmit={crear} className="flex gap-2">
        <input
          value={etiqueta}
          onChange={(e) => setEtiqueta(e.target.value)}
          placeholder="Nombre (opcional)"
          aria-label="Nombre de la API Key"
          className="flex-1 rounded border border-slate-300 px-3 py-1 text-sm"
        />
        <button type="submit" className="rounded bg-slate-900 px-3 py-1 text-sm text-white">Crear API Key</button>
      </form>

      {error && <p role="alert" className="text-sm text-red-600">{error}</p>}
    </section>
  )
}
