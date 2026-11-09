package cr.ac.tec.ic7602.javaapi;

import java.util.Comparator;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Guarda los productos en memoria. Jetty atiende cada petición en su propio hilo,
 * por eso el mapa es concurrente y los ids salen de un contador atómico.
 * Al reiniciar el pod se pierde todo, igual que la caché: el enunciado no pide persistencia.
 */
public class RepositorioProductos {

    private final ConcurrentHashMap<Long, Producto> productos = new ConcurrentHashMap<>();
    private final AtomicLong siguienteId = new AtomicLong(1);

    public List<Producto> listar() {
        return productos.values().stream()
                .sorted(Comparator.comparingLong(Producto::id))
                .toList();
    }

    public Optional<Producto> buscar(long id) {
        return Optional.ofNullable(productos.get(id));
    }

    public Producto crear(String nombre, double precio) {
        long id = siguienteId.getAndIncrement();
        Producto producto = new Producto(id, nombre, precio);
        productos.put(id, producto);
        return producto;
    }

    /** Reemplaza el producto completo. Vacío si no existe; PUT no crea productos nuevos. */
    public Optional<Producto> reemplazar(long id, String nombre, double precio) {
        return Optional.ofNullable(productos.computeIfPresent(id, (llave, viejo) -> new Producto(id, nombre, precio)));
    }

    public boolean eliminar(long id) {
        return productos.remove(id) != null;
    }
}
