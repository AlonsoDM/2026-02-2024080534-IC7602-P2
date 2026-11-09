package cr.ac.tec.ic7602.javaapi;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.javalin.Javalin;
import io.javalin.testtools.JavalinTest;
import java.io.IOException;
import java.util.Map;
import okhttp3.MediaType;
import okhttp3.RequestBody;
import okhttp3.Response;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class ProductosApiTest {

    private static final ObjectMapper JSON = new ObjectMapper();
    private static final MediaType TIPO_JSON = MediaType.parse("application/json");

    private RepositorioProductos repositorio;
    private Javalin app;

    @BeforeEach
    void preparar() {
        repositorio = new RepositorioProductos();
        repositorio.crear("Teclado", 25.5);
        app = App.crear(repositorio);
    }

    private static JsonNode cuerpo(Response respuesta) throws IOException {
        return JSON.readTree(respuesta.body().string());
    }

    @Test
    void healthResponde200() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response respuesta = cliente.get("/health");
            assertEquals(200, respuesta.code());
            assertEquals("ok", cuerpo(respuesta).get("estado").asText());
        });
    }

    @Test
    void getListaDevuelveJson() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response respuesta = cliente.get("/api/productos");
            assertEquals(200, respuesta.code());
            // La caché decide si guarda con el Content-Type cuando la ruta no tiene extensión
            assertTrue(respuesta.header("Content-Type").startsWith("application/json"));
            JsonNode lista = cuerpo(respuesta);
            assertEquals(1, lista.size());
            assertEquals("Teclado", lista.get(0).get("nombre").asText());
        });
    }

    @Test
    void getUnoDevuelveElProductoO404() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response encontrado = cliente.get("/api/productos/1");
            assertEquals(200, encontrado.code());
            assertEquals(25.5, cuerpo(encontrado).get("precio").asDouble());

            Response ausente = cliente.get("/api/productos/99");
            assertEquals(404, ausente.code());
            assertTrue(cuerpo(ausente).get("error").asText().contains("99"));
        });
    }

    @Test
    void idQueNoEsEnteroPositivoDa400() {
        JavalinTest.test(app, (servidor, cliente) -> {
            assertEquals(400, cliente.get("/api/productos/abc").code());
            assertEquals(400, cliente.get("/api/productos/0").code());
            assertEquals(400, cliente.delete("/api/productos/-3").code());
        });
    }

    @Test
    void postCreaConLocationY201() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response respuesta = cliente.post("/api/productos", Map.of("nombre", "  Mouse ", "precio", 12));
            assertEquals(201, respuesta.code());
            assertEquals("/api/productos/2", respuesta.header("Location"));
            JsonNode creado = cuerpo(respuesta);
            assertEquals(2, creado.get("id").asLong());
            assertEquals("Mouse", creado.get("nombre").asText());

            assertEquals(2, cuerpo(cliente.get("/api/productos")).size());
        });
    }

    @Test
    void postConCuerpoInvalidoDa400SinCrearNada() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response malFormado = cliente.request("/api/productos",
                    peticion -> peticion.post(RequestBody.create("{no es json", TIPO_JSON)));
            assertEquals(400, malFormado.code());

            assertEquals(400, cliente.post("/api/productos", Map.of("precio", 3)).code());
            assertEquals(400, cliente.post("/api/productos", Map.of("nombre", " ", "precio", 3)).code());
            assertEquals(400, cliente.post("/api/productos", Map.of("nombre", "Cable")).code());
            assertEquals(400, cliente.post("/api/productos", Map.of("nombre", "Cable", "precio", -1)).code());

            assertEquals(1, repositorio.listar().size());
        });
    }

    @Test
    void putReemplazaO404() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response respuesta = cliente.put("/api/productos/1", Map.of("nombre", "Teclado mecánico", "precio", 60));
            assertEquals(200, respuesta.code());
            assertEquals("Teclado mecánico", cuerpo(respuesta).get("nombre").asText());
            assertEquals(60.0, repositorio.buscar(1).orElseThrow().precio());

            // PUT no crea: un id que no existe sigue sin existir
            assertEquals(404, cliente.put("/api/productos/7", Map.of("nombre", "X", "precio", 1)).code());
            assertTrue(repositorio.buscar(7).isEmpty());

            assertEquals(400, cliente.put("/api/productos/1", Map.of("nombre", "X")).code());
        });
    }

    @Test
    void deleteBorraY204LaSegundaVez404() {
        JavalinTest.test(app, (servidor, cliente) -> {
            Response borrado = cliente.delete("/api/productos/1");
            assertEquals(204, borrado.code());
            assertEquals("", borrado.body().string());

            assertEquals(404, cliente.delete("/api/productos/1").code());
            assertEquals(404, cliente.get("/api/productos/1").code());
        });
    }
}
