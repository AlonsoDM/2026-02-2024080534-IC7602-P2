import type { SupabaseClient } from '@supabase/supabase-js'
import type { CreateSupabase } from '../src/supabase'

/*
  Supabase falso para las pruebas: cada tabla es un arreglo de filas en memoria.

  Solo entiende lo que usa el Worker: select (no recorta columnas), insert, update, delete, eq (también
  con ruta, como 'domains.verified'), gt, contains, limit, order y maybeSingle. `paises` simula
  pais_de_ip y verificar_url_user compara contra el campo `password` de url_users, en texto plano.
  Con `fallar` la tabla o función responde con error, como si Supabase se cayera.
*/
type Tablas = Record<string, Record<string, unknown>[]>

export function fakeSupabase(tablas: Tablas, fallar: string[] = [], paises: Record<string, string> = {}): CreateSupabase {
  return () => ({
    from: (tabla: string) => consulta(tabla, (tablas[tabla] ??= []), fallar),
    rpc: (nombre: string, args: Record<string, unknown>) => {
      if (fallar.includes(nombre)) {
        return Promise.resolve({ data: null, error: { message: `fallo simulado en ${nombre}` } })
      }
      if (nombre === 'pais_de_ip') {
        return Promise.resolve({ data: paises[args.p_ip as string] ?? 'ZZ', error: null })
      }
      if (nombre === 'verificar_url_user') {
        const url = (tablas.urls ?? []).find((u) => u.id === args.p_url_id)
        const usuario = (tablas.url_users ?? []).find(
          (u) => u.url_id === args.p_url_id && u.username === args.p_username && u.password === args.p_password,
        )
        return Promise.resolve({ data: url?.auth === 'user_password' && usuario ? usuario.id : null, error: null })
      }
      throw new Error(`RPC no implementado: ${nombre}`)
    },
  }) as unknown as SupabaseClient
}

function consulta(tabla: string, filas: Record<string, unknown>[], fallar: string[]) {
  let resultado = [...filas]
  let borrar = false
  let cambios: Record<string, unknown> | null = null
  const error = fallar.includes(tabla) ? { message: `fallo simulado en ${tabla}` } : null

  const q = {
    select: () => q,
    insert: (fila: Record<string, unknown>) => {
      if (!error) filas.push(fila)
      return Promise.resolve({ data: null, error })
    },
    // Se actualiza al hacer await, después de aplicar los filtros
    update: (valores: Record<string, unknown>) => {
      cambios = valores
      return q
    },
    // Se borra al hacer await, después de aplicar los filtros
    delete: () => {
      borrar = true
      return q
    },
    gt: (columna: string, valor: string) => {
      resultado = resultado.filter((f) => String(leer(f, columna)) > valor)
      return q
    },
    eq: (columna: string, valor: unknown) => {
      resultado = resultado.filter((f) => leer(f, columna) === valor)
      return q
    },
    contains: (columna: string, valores: unknown[]) => {
      resultado = resultado.filter((f) => {
        const arreglo = leer(f, columna)
        return Array.isArray(arreglo) && valores.every((valor) => arreglo.includes(valor))
      })
      return q
    },
    limit: (cantidad: number) => {
      resultado = resultado.slice(0, cantidad)
      return q
    },
    order: (columna: string) => {
      resultado.sort((a, b) => Number(leer(a, columna)) - Number(leer(b, columna)))
      return q
    },
    maybeSingle: () => Promise.resolve(error ? { data: null, error } : { data: resultado[0] ?? null, error: null }),
    then: (ok: (r: unknown) => unknown, mal?: (e: unknown) => unknown) => {
      if (borrar && !error) {
        for (const fila of resultado) filas.splice(filas.indexOf(fila), 1)
      }
      if (cambios && !error) {
        for (const fila of resultado) Object.assign(fila, cambios)
      }
      return Promise.resolve(error ? { data: null, error } : { data: resultado, error: null }).then(ok, mal)
    },
  }
  return q
}

function leer(fila: Record<string, unknown>, ruta: string): unknown {
  return ruta.split('.').reduce<unknown>((v, k) => (v as Record<string, unknown> | undefined)?.[k], fila)
}
