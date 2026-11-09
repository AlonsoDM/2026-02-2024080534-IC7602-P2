package cr.ac.tec.ic7602.javaapi;

import java.util.Map;

/**
 * Configuración del API leída de variables de entorno. No hay valores por defecto:
 * si falta una variable o no sirve, el API no arranca.
 */
public record Configuracion(int puerto) {

    static final String VARIABLE_PUERTO = "JAVA_API_PORT";

    /** Lee la configuración de un mapa de variables, normalmente {@code System.getenv()}. */
    public static Configuracion desde(Map<String, String> entorno) {
        String valor = entorno.get(VARIABLE_PUERTO);
        if (valor == null || valor.isBlank()) {
            throw new IllegalArgumentException("Falta la variable de entorno " + VARIABLE_PUERTO);
        }

        int puerto;
        try {
            puerto = Integer.parseInt(valor.trim());
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException(
                    VARIABLE_PUERTO + " debe ser un entero, se recibió \"" + valor + "\"");
        }

        if (puerto < 1 || puerto > 65535) {
            throw new IllegalArgumentException(
                    VARIABLE_PUERTO + " debe estar entre 1 y 65535, se recibió " + puerto);
        }
        return new Configuracion(puerto);
    }
}
