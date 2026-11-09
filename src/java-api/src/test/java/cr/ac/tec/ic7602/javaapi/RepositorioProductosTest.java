package cr.ac.tec.ic7602.javaapi;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.jupiter.api.Test;

class RepositorioProductosTest {

    @Test
    void creaConIdsSeguidosYListaOrdenado() {
        RepositorioProductos repositorio = new RepositorioProductos();
        repositorio.crear("A", 1);
        repositorio.crear("B", 2);
        repositorio.crear("C", 3);

        List<Long> ids = repositorio.listar().stream().map(Producto::id).toList();
        assertEquals(List.of(1L, 2L, 3L), ids);
    }

    @Test
    void reemplazarSoloCambiaLosQueExisten() {
        RepositorioProductos repositorio = new RepositorioProductos();
        repositorio.crear("A", 1);

        assertEquals(new Producto(1, "Z", 9), repositorio.reemplazar(1, "Z", 9).orElseThrow());
        assertTrue(repositorio.reemplazar(2, "Y", 1).isEmpty());
        assertEquals(1, repositorio.listar().size());
    }

    @Test
    void eliminarDiceSiExistia() {
        RepositorioProductos repositorio = new RepositorioProductos();
        repositorio.crear("A", 1);

        assertTrue(repositorio.eliminar(1));
        assertFalse(repositorio.eliminar(1));
        assertTrue(repositorio.buscar(1).isEmpty());
    }

    @Test
    void variosHilosCreandoNoRepitenIds() throws InterruptedException {
        RepositorioProductos repositorio = new RepositorioProductos();
        List<Thread> hilos = new ArrayList<>();
        for (int i = 0; i < 8; i++) {
            Thread hilo = new Thread(() -> {
                for (int j = 0; j < 250; j++) {
                    repositorio.crear("p", j);
                }
            });
            hilos.add(hilo);
            hilo.start();
        }
        for (Thread hilo : hilos) {
            hilo.join();
        }

        Set<Long> ids = new HashSet<>();
        repositorio.listar().forEach(producto -> ids.add(producto.id()));
        assertEquals(2000, ids.size());
        assertEquals(2000, repositorio.listar().size());
    }
}
