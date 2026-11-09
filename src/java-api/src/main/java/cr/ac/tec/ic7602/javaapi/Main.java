package cr.ac.tec.ic7602.javaapi;

public final class Main {

    private Main() {
    }

    public static void main(String[] args) {
        Configuracion configuracion;
        try {
            configuracion = Configuracion.desde(System.getenv());
        } catch (IllegalArgumentException e) {
            System.err.println(e.getMessage());
            System.exit(1);
            return;
        }

        // Unos productos de arranque para que el primer GET ya tenga algo que guardar en la caché
        RepositorioProductos repositorio = new RepositorioProductos();
        repositorio.crear("Teclado", 25.5);
        repositorio.crear("Mouse", 12.0);
        repositorio.crear("Monitor", 180.0);

        App.crear(repositorio).start(configuracion.puerto());
    }
}
