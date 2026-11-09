package cr.ac.tec.ic7602.javaapi;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import org.junit.jupiter.api.Test;

class ConfiguracionTest {

    @Test
    void leeElPuertoDeLaVariable() {
        assertEquals(8080, Configuracion.desde(Map.of("JAVA_API_PORT", "8080")).puerto());
        assertEquals(1, Configuracion.desde(Map.of("JAVA_API_PORT", " 1 ")).puerto());
        assertEquals(65535, Configuracion.desde(Map.of("JAVA_API_PORT", "65535")).puerto());
    }

    @Test
    void sinVariableNoArranca() {
        IllegalArgumentException error = assertThrows(IllegalArgumentException.class,
                () -> Configuracion.desde(Map.of()));
        assertTrue(error.getMessage().contains("JAVA_API_PORT"));

        assertThrows(IllegalArgumentException.class, () -> Configuracion.desde(Map.of("JAVA_API_PORT", "")));
    }

    @Test
    void puertoQueNoSirveNoArranca() {
        assertThrows(IllegalArgumentException.class, () -> Configuracion.desde(Map.of("JAVA_API_PORT", "ocho")));
        assertThrows(IllegalArgumentException.class, () -> Configuracion.desde(Map.of("JAVA_API_PORT", "0")));
        assertThrows(IllegalArgumentException.class, () -> Configuracion.desde(Map.of("JAVA_API_PORT", "65536")));
        assertThrows(IllegalArgumentException.class, () -> Configuracion.desde(Map.of("JAVA_API_PORT", "80.5")));
    }
}
