package cr.ac.tec.ic7602.javaapi;

import io.javalin.Javalin;
import io.javalin.http.Context;
import io.javalin.http.HttpStatus;
import java.util.Map;
import java.util.Optional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Rutas del REST API de prueba. Es uno de los cuatro orígenes detrás de la Zonal Cache:
 * los GET responden JSON con 200 para que la caché los pueda guardar, y POST, PUT y DELETE
 * sirven para comprobar que la caché reenvía el método y el cuerpo sin guardarlos.
 *
 * <pre>
 * GET    /health               200
 * GET    /api/productos        200 con la lista
 * GET    /api/productos/{id}   200, o 404 si no existe
 * POST   /api/productos        201 con Location, o 400 si el cuerpo no sirve
 * PUT    /api/productos/{id}   200, 400 o 404
 * DELETE /api/productos/{id}   204, o 404 si no existe
 * </pre>
 */
public final class App {

    private static final Logger LOG = LoggerFactory.getLogger(App.class);
    static final String RUTA = "/api/productos";

    /** Cuerpo de POST y PUT. Se usan objetos para distinguir un campo ausente de un cero. */
    record DatosProducto(String nombre, Double precio) {
    }

    private App() {
    }

    public static Javalin crear(RepositorioProductos repositorio) {
        Javalin app = Javalin.create(config -> {
            config.showJavalinBanner = false;
            // Cada petición queda en el log, así se ve cuándo la caché sí llegó al origen
            config.requestLogger.http((ctx, ms) ->
                    LOG.info("{} {} -> {} ({} ms)", ctx.method(), ctx.path(), ctx.statusCode(), Math.round(ms)));
        });

        app.get("/health", ctx -> ctx.json(Map.of("estado", "ok")));

        app.get(RUTA, ctx -> ctx.json(repositorio.listar()));

        app.get(RUTA + "/{id}", ctx -> {
            Optional<Long> id = leerId(ctx);
            if (id.isEmpty()) {
                return;
            }
            repositorio.buscar(id.get()).ifPresentOrElse(ctx::json, () -> noExiste(ctx, id.get()));
        });

        app.post(RUTA, ctx -> {
            Optional<DatosProducto> datos = leerDatos(ctx);
            if (datos.isEmpty()) {
                return;
            }
            Producto creado = repositorio.crear(datos.get().nombre().trim(), datos.get().precio());
            ctx.status(HttpStatus.CREATED);
            ctx.header("Location", RUTA + "/" + creado.id());
            ctx.json(creado);
        });

        app.put(RUTA + "/{id}", ctx -> {
            Optional<Long> id = leerId(ctx);
            if (id.isEmpty()) {
                return;
            }
            Optional<DatosProducto> datos = leerDatos(ctx);
            if (datos.isEmpty()) {
                return;
            }
            repositorio.reemplazar(id.get(), datos.get().nombre().trim(), datos.get().precio())
                    .ifPresentOrElse(ctx::json, () -> noExiste(ctx, id.get()));
        });

        app.delete(RUTA + "/{id}", ctx -> {
            Optional<Long> id = leerId(ctx);
            if (id.isEmpty()) {
                return;
            }
            if (repositorio.eliminar(id.get())) {
                ctx.status(HttpStatus.NO_CONTENT);
            } else {
                noExiste(ctx, id.get());
            }
        });

        return app;
    }

    // Vacío si el id no es un entero positivo, y en ese caso ya dejó el 400
    private static Optional<Long> leerId(Context ctx) {
        String crudo = ctx.pathParam("id");
        try {
            long id = Long.parseLong(crudo);
            if (id > 0) {
                return Optional.of(id);
            }
        } catch (NumberFormatException e) {
            // cae al 400 de abajo
        }
        error(ctx, HttpStatus.BAD_REQUEST, "El id debe ser un entero positivo, se recibió \"" + crudo + "\"");
        return Optional.empty();
    }

    // Vacío si el JSON no se puede leer o no trae un nombre y un precio válidos, y en ese caso ya dejó el 400
    private static Optional<DatosProducto> leerDatos(Context ctx) {
        DatosProducto datos;
        try {
            datos = ctx.bodyAsClass(DatosProducto.class);
        } catch (Exception e) {
            error(ctx, HttpStatus.BAD_REQUEST, "El cuerpo debe ser un JSON con nombre y precio");
            return Optional.empty();
        }
        if (datos == null || datos.nombre() == null || datos.nombre().isBlank()) {
            error(ctx, HttpStatus.BAD_REQUEST, "Falta el nombre del producto");
            return Optional.empty();
        }
        if (datos.precio() == null || datos.precio().isNaN() || datos.precio().isInfinite() || datos.precio() < 0) {
            error(ctx, HttpStatus.BAD_REQUEST, "El precio debe ser un número mayor o igual a cero");
            return Optional.empty();
        }
        return Optional.of(datos);
    }

    private static void noExiste(Context ctx, long id) {
        error(ctx, HttpStatus.NOT_FOUND, "No existe el producto " + id);
    }

    private static void error(Context ctx, HttpStatus estado, String mensaje) {
        ctx.status(estado);
        ctx.json(Map.of("error", mensaje));
    }
}
