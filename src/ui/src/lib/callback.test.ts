import { describe, expect, it } from 'vitest'
import { destinoConSesion, leerParametros } from './callback'

// Así arma la caché el enlace: callback codificado con %XX y la ruta original dentro de él, dos veces
const enlace = (url: string, callback: string) => `?url=${url}&callback=${encodeURIComponent(callback)}`

describe('leerParametros', () => {
  it('acepta el enlace que arma la caché por HTTP y por HTTPS', () => {
    for (const cb of [
      'http://privado.apache.test/__auth/callback?next=%2Fget',
      'https://privado.externo-https.test/__auth/callback?next=%2Fa%2Fb%3Fx%3D1',
      'http://localhost:8080/__auth/callback?next=%2F',
    ]) {
      const r = leerParametros(enlace('4', cb))
      expect(r.ok).toBe(true)
      if (r.ok) {
        expect(r.urlId).toBe(4)
        expect(r.callback.pathname).toBe('/__auth/callback')
      }
    }
  })

  it('conserva la ruta original que va dentro de next', () => {
    const r = leerParametros(enlace('8', 'https://privado.x.test/__auth/callback?next=%2Fa%2Fb%3Fx%3D1'))
    expect(r.ok && r.callback.searchParams.get('next')).toBe('/a/b?x=1')
  })

  it.each([
    ['sin url', '?callback=' + encodeURIComponent('http://a.test/__auth/callback')],
    ['sin callback', '?url=4'],
    ['url que no es número', enlace('abc', 'http://a.test/__auth/callback')],
    ['url cero', enlace('0', 'http://a.test/__auth/callback')],
    ['url negativa', enlace('-3', 'http://a.test/__auth/callback')],
    ['url decimal', enlace('1.5', 'http://a.test/__auth/callback')],
    ['url gigante', enlace('99999999999999999999', 'http://a.test/__auth/callback')],
    ['callback que no es una dirección', enlace('4', 'no es una url')],
  ])('rechaza %s', (_nombre, busqueda) => {
    expect(leerParametros(busqueda).ok).toBe(false)
  })

  it.each([
    ['otro esquema', 'javascript:alert(1)//__auth/callback'],
    ['ftp', 'ftp://a.test/__auth/callback'],
    ['otra ruta', 'https://a.test/robar'],
    ['la raíz', 'https://a.test/'],
    ['ruta que solo empieza igual', 'https://a.test/__auth/callback/../otra'],
    ['ruta con prefijo', 'https://a.test/x/__auth/callback'],
    ['credenciales en la dirección', 'https://sitio.test@evil.test/__auth/callback'],
    ['usuario y clave', 'https://u:p@evil.test/__auth/callback'],
    ['una sesión ya puesta', 'https://a.test/__auth/callback?next=%2F&session=abc'],
    ['un fragmento', 'https://a.test/__auth/callback?next=%2F#x'],
  ])('rechaza un callback con %s', (_nombre, callback) => {
    expect(leerParametros(enlace('4', callback)).ok).toBe(false)
  })
})

describe('destinoConSesion', () => {
  it('agrega la sesión con & cuando el callback ya trae next', () => {
    const r = leerParametros(enlace('4', 'https://a.test/__auth/callback?next=%2Fget'))
    if (!r.ok) throw new Error('debía ser válido')
    expect(destinoConSesion(r.callback, 'abc123')).toBe('https://a.test/__auth/callback?next=%2Fget&session=abc123')
  })

  it('agrega la sesión con ? cuando no hay query', () => {
    const r = leerParametros(enlace('4', 'https://a.test/__auth/callback'))
    if (!r.ok) throw new Error('debía ser válido')
    expect(destinoConSesion(r.callback, 'abc123')).toBe('https://a.test/__auth/callback?session=abc123')
  })

  it('el destino conserva el host de la caché', () => {
    const r = leerParametros(enlace('4', 'http://privado.apache.test/__auth/callback?next=%2Fget'))
    if (!r.ok) throw new Error('debía ser válido')
    expect(new URL(destinoConSesion(r.callback, 'abc')).host).toBe('privado.apache.test')
  })
})
