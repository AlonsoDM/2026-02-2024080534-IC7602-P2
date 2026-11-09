package cr.ac.tec.ic7602.javaapi;

/** Un producto del recurso en memoria. El id lo asigna el repositorio. */
public record Producto(long id, String nombre, double precio) {
}
