// Escribe la hora en que corrió el JS en el navegador, para ver que el archivo llegó completo
document.addEventListener("DOMContentLoaded", function () {
    var estado = document.getElementById("estado");
    estado.textContent = "El JS corrió a las " + new Date().toLocaleTimeString("es-CR") + ".";
});
