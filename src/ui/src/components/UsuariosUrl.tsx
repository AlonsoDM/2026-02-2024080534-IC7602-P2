import { useEffect, useState, type FormEvent } from 'react'
import { supabase } from '../lib/supabase'

type Usuario = { id: number; username: string; created_at: string }

const MINIMO_CLAVE = 8

// Usuarios y contraseñas de una URL con auth = user_password. La contraseña nunca se lee: se crea y se cambia
// con las funciones crear_url_user y cambiar_password_url_user, que la guardan con bcrypt dentro de Supabase.
export default function UsuariosUrl({ urlId }: { urlId: number }) {
  const [usuarios, setUsuarios] = useState<Usuario[]>([])
  const [nombre, setNombre] = useState('')
  const [clave, setClave] = useState('')
  const [editando, setEditando] = useState<Usuario | null>(null)
  const [nombreEditado, setNombreEditado] = useState('')
  const [claveEditada, setClaveEditada] = useState('')
  const [error, setError] = useState('')

  // Cada cambio de este número vuelve a leer la lista
  const [version, setVersion] = useState(0)
  const recargar = () => setVersion((v) => v + 1)

  useEffect(() => {
    let activo = true
    supabase
      .from('url_users')
      .select('id, username, created_at')
      .eq('url_id', urlId)
      .order('id')
      .then(({ data, error }) => {
        if (!activo) return
        if (error) setError('No se pudieron cargar los usuarios.')
        else setUsuarios(data as Usuario[])
      })
    return () => {
      activo = false
    }
  }, [urlId, version])

  async function crear(e: FormEvent) {
    e.preventDefault()
    setError('')
    if (!nombre.trim()) return setError('Escriba un nombre de usuario.')
    if (clave.length < MINIMO_CLAVE) return setError(`La contraseña debe tener al menos ${MINIMO_CLAVE} caracteres.`)
    const { error } = await supabase.rpc('crear_url_user', { p_url_id: urlId, p_username: nombre.trim(), p_password: clave })
    if (error) {
      setError(error.code === '23505' ? 'Ya existe un usuario con ese nombre.' : 'No se pudo crear el usuario.')
      return
    }
    setNombre('')
    setClave('')
    recargar()
  }

  function empezarEdicion(u: Usuario) {
    setEditando(u)
    setNombreEditado(u.username)
    setClaveEditada('')
    setError('')
  }

  async function guardarEdicion(e: FormEvent) {
    e.preventDefault()
    if (!editando) return
    setError('')
    if (!nombreEditado.trim()) return setError('Escriba un nombre de usuario.')
    if (claveEditada !== '' && claveEditada.length < MINIMO_CLAVE) {
      return setError(`La contraseña debe tener al menos ${MINIMO_CLAVE} caracteres.`)
    }
    const { error } = await supabase.rpc('cambiar_password_url_user', {
      p_id: editando.id,
      p_username: nombreEditado.trim(),
      // Vacía significa "dejar la contraseña como está"
      p_password: claveEditada === '' ? null : claveEditada,
    })
    if (error) {
      setError(error.code === '23505' ? 'Ya existe un usuario con ese nombre.' : 'No se pudo modificar el usuario.')
      return
    }
    setEditando(null)
    recargar()
  }

  async function eliminar(u: Usuario) {
    if (!window.confirm(`¿Eliminar al usuario ${u.username}? Sus sesiones abiertas también se cierran.`)) return
    const { error } = await supabase.from('url_users').delete().eq('id', u.id)
    if (error) setError('No se pudo eliminar el usuario.')
    else recargar()
  }

  const campo = 'rounded border border-slate-300 px-3 py-1 text-sm'

  return (
    <section className="space-y-3 rounded bg-slate-50 p-3" aria-label="Usuarios">
      <h3 className="text-sm font-semibold text-slate-800">Usuarios y contraseñas</h3>

      {usuarios.length === 0 ? (
        <p className="text-sm text-slate-500">Esta URL todavía no tiene usuarios: nadie puede entrar.</p>
      ) : (
        <ul className="divide-y divide-slate-200 text-sm">
          {usuarios.map((u) => (
            <li key={u.id} className="py-2">
              {editando?.id === u.id ? (
                <form onSubmit={guardarEdicion} className="flex flex-wrap items-center gap-2">
                  <input value={nombreEditado} onChange={(e) => setNombreEditado(e.target.value)} aria-label="Usuario" className={campo} />
                  <input
                    type="password"
                    value={claveEditada}
                    onChange={(e) => setClaveEditada(e.target.value)}
                    placeholder="Nueva contraseña (opcional)"
                    autoComplete="new-password"
                    aria-label="Nueva contraseña"
                    className={campo}
                  />
                  <button type="submit" className="rounded bg-slate-900 px-3 py-1 text-white">Guardar</button>
                  <button type="button" onClick={() => setEditando(null)} className="rounded border border-slate-300 px-3 py-1">Cancelar</button>
                </form>
              ) : (
                <div className="flex items-center justify-between gap-2">
                  <span className="font-mono">{u.username}</span>
                  <span className="flex gap-2">
                    <button onClick={() => empezarEdicion(u)} className="rounded border border-slate-300 px-2 py-1">Modificar</button>
                    <button onClick={() => eliminar(u)} className="rounded border border-red-300 px-2 py-1 text-red-700">Eliminar</button>
                  </span>
                </div>
              )}
            </li>
          ))}
        </ul>
      )}

      <form onSubmit={crear} className="flex flex-wrap gap-2">
        <input value={nombre} onChange={(e) => setNombre(e.target.value)} placeholder="Usuario" aria-label="Nuevo usuario" className={campo} />
        <input
          type="password"
          value={clave}
          onChange={(e) => setClave(e.target.value)}
          placeholder={`Contraseña (mín. ${MINIMO_CLAVE})`}
          autoComplete="new-password"
          aria-label="Contraseña del nuevo usuario"
          className={campo}
        />
        <button type="submit" className="rounded bg-slate-900 px-3 py-1 text-sm text-white">Crear usuario</button>
      </form>

      {error && <p role="alert" className="text-sm text-red-600">{error}</p>}
    </section>
  )
}
