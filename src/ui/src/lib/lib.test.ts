import { describe, expect, it } from 'vitest'
import { bytesAMb, dividirTtl, formatearBytes, formatearTtl, mbABytes } from './formato'
import { generarLlave, sha256Hex } from './llaves'
import { normalizarExtension, validarOrigen, validarPatron, validarTipos } from './validar'

describe('validarPatron', () => {
  it.each(['marca.com', 'www.marca.com', '*.marca.com', 'a.b.marca.com', '*.api.marca.com'])('acepta %s', (p) => {
    expect(validarPatron(p, 'marca.com')).toBeNull()
  })

  it.each(['otramarca.com', 'marca.com.otro.com', '*.otro.com', 'Marca.com', 'marca.*.com', '*marca.com', '', 'ma rca.com'])(
    'rechaza %s',
    (p) => {
      expect(validarPatron(p, 'marca.com')).not.toBeNull()
    },
  )

  it('no deja pasar un dominio que solo termina igual', () => {
    // "xmarca.com" termina en "marca.com" pero no es un subdominio suyo
    expect(validarPatron('xmarca.com', 'marca.com')).not.toBeNull()
  })
})

describe('validarOrigen', () => {
  it.each(['http://java-api:8080', 'https://origen.marca.com', 'http://httpbin.org/ruta'])('acepta %s', (o) => {
    expect(validarOrigen(o)).toBeNull()
  })
  it.each(['origen.marca.com', 'ftp://origen.marca.com', 'file:///etc/passwd', 'javascript:alert(1)', '', 'http://'])(
    'rechaza %s',
    (o) => {
      expect(validarOrigen(o)).not.toBeNull()
    },
  )
})

describe('tipos de archivo', () => {
  it('normaliza la extensión', () => {
    expect(normalizarExtension(' .PNG ')).toBe('png')
  })
  it('acepta una lista válida', () => {
    expect(validarTipos([{ extension: 'png', ttl_seconds: 60 }, { extension: 'js', ttl_seconds: 3600 }])).toBeNull()
  })
  it('exige al menos un tipo', () => {
    expect(validarTipos([])).not.toBeNull()
  })
  it('rechaza extensiones repetidas, vacías o con símbolos', () => {
    expect(validarTipos([{ extension: 'png', ttl_seconds: 1 }, { extension: 'png', ttl_seconds: 2 }])).toContain('repetida')
    expect(validarTipos([{ extension: '', ttl_seconds: 1 }])).not.toBeNull()
    expect(validarTipos([{ extension: 'p/g', ttl_seconds: 1 }])).not.toBeNull()
  })
  it('rechaza un TTL cero, negativo, decimal o que no es número', () => {
    for (const ttl of [0, -5, 1.5, NaN]) {
      expect(validarTipos([{ extension: 'png', ttl_seconds: ttl }])).not.toBeNull()
    }
  })
})

describe('formato', () => {
  it('convierte MB a bytes y de vuelta', () => {
    expect(mbABytes(2)).toBe(2097152)
    expect(mbABytes(0.5)).toBe(524288)
    expect(bytesAMb(2097152)).toBe(2)
  })
  it('muestra los bytes en la unidad que corresponde', () => {
    expect(formatearBytes(2097152)).toBe('2 MB')
    expect(formatearBytes(1536)).toBe('1.5 KB')
    expect(formatearBytes(500)).toBe('500 B')
  })
  it('elige la unidad más grande que divide el TTL exactamente', () => {
    expect(dividirTtl(86400)).toEqual({ cantidad: 1, segundos: 86400 })
    expect(dividirTtl(7200)).toEqual({ cantidad: 2, segundos: 3600 })
    expect(dividirTtl(90)).toEqual({ cantidad: 90, segundos: 1 })
    expect(formatearTtl(300)).toBe('5 minutos')
    expect(formatearTtl(45)).toBe('45 segundos')
    expect(formatearTtl(1)).toBe('1 segundo')
    expect(formatearTtl(60)).toBe('1 minuto')
    expect(formatearTtl(3600)).toBe('1 hora')
    expect(formatearTtl(86400)).toBe('1 día')
  })
})

describe('llaves', () => {
  it('sha256Hex da el hash conocido', async () => {
    // SHA-256 de "abc" según el estándar; el Worker usa el mismo cálculo
    expect(await sha256Hex('abc')).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
  })
  it('generarLlave da zc_ más 64 hexadecimales, distintos cada vez', () => {
    const a = generarLlave()
    expect(a).toMatch(/^zc_[0-9a-f]{64}$/)
    expect(generarLlave()).not.toBe(a)
  })
})
