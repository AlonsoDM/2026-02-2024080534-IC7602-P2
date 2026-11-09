// Prueba contra un API en ejecución con los dominios, zonas y datos GEO del seed.
import assert from 'node:assert/strict'
import { isIPv4 } from 'node:net'

const { NODE_API_URL, NODE_API_KEY } = process.env
assert(NODE_API_URL && NODE_API_KEY, 'Definir NODE_API_URL y NODE_API_KEY del Interceptor')
const domain = process.env.INTERCEPTOR_TEST_DOMAIN || 'apache.test'

async function resolve(domain, client_ip) {
  const url = new URL('/api/interceptor/resolve', NODE_API_URL)
  url.search = new URLSearchParams({ domain, client_ip }).toString()
  return fetch(url, {
    headers: { 'x-api-key': NODE_API_KEY },
    signal: AbortSignal.timeout(10000),
  })
}

for (const [ip, zone] of [['8.8.8.8', 'US'], ['80.58.61.250', 'ES'], ['81.2.69.160', 'GB']]) {
  const res = await resolve(domain, ip)
  assert.equal(res.status, 200, `Resolución de ${ip}`)
  const body = await res.json()
  assert.equal(body.exists, true)
  assert.equal(body.zone, zone)
  assert(isIPv4(body.address))
  console.log(`${ip} → ${zone} → ${body.address}`)
}

const absent = await resolve('no-configurado.invalid', '8.8.8.8')
assert.equal(absent.status, 200)
assert.deepEqual(await absent.json(), { exists: false })
assert.equal((await resolve(domain, 'invalida')).status, 400)
console.log('GEO de tres países, dominio ausente y validación: OK')
