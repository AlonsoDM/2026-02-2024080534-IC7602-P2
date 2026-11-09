import { Hono } from 'hono'
import { cors } from './middleware/cors'
import { auth } from './routes/auth'
import { cache } from './routes/cache'
import { domains } from './routes/domains'
import { interceptor } from './routes/interceptor'
import { createSupabase, type CreateSupabase } from './supabase'
import type { AppEnv } from './types'

// Las pruebas pasan un createSupabase falso para no depender del proyecto real
export function createApp(supabase: CreateSupabase = createSupabase) {
  const app = new Hono<AppEnv>()

  // Sin autenticación: solo confirma que el Worker responde
  app.get('/health', (c) => c.json({ status: 'ok Im alive and well' }))

  // middleware de CORS se ejecuta para las rutas que coincidan con /api/*
  // Va antes que los demas middleware para que el browser pueda pueda comprobar los permisos CORS antes de enviar una x-api-key
  app.use('/api/*', cors)

  // Todas las rutas de la API usan el cliente de Supabase
  app.use('/api/*', async (c, next) => {
    c.set('supabase', supabase(c.env))
    await next()
  })

  // Un grupo por componente; cada archivo de routes/ pone su propio requireScope
  app.route('/api/cache', cache)
  app.route('/api/interceptor', interceptor)
  app.route('/api/auth', auth)
  app.route('/api/domains', domains)

  app.notFound((c) => c.json({ error: 'Ruta no encontrada' }, 404))

  // Un error no esperado (por ejemplo, faltan los secrets) responde JSON y queda en los logs de Cloudflare
  app.onError((err, c) => {
    console.error(err)
    return c.json({ error: 'Error interno' }, 500)
  })

  return app
}

export default createApp()
