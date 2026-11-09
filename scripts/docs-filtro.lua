local LARGO_MINIMO = 12
local SEPARADORES = '[/%-_%.]'
local ANCHO_MAXIMO = 40
local AIRE = 5

local function Code(elemento)
    if #elemento.text <= LARGO_MINIMO then
        return nil
    end

    local trozos = {}
    local pendiente = ''

    for caracter in elemento.text:gmatch('.') do
        pendiente = pendiente .. caracter

        if caracter:match(SEPARADORES) then
            table.insert(trozos, pandoc.Code(pendiente, elemento.attr))
            table.insert(trozos, pandoc.RawInline('latex', '\\allowbreak{}'))
            pendiente = ''
        end
    end

    if #trozos == 0 then
        return nil
    end

    if pendiente ~= '' then
        table.insert(trozos, pandoc.Code(pendiente, elemento.attr))
    end

    return trozos
end

local function medir(filas, anchos)
    for _, fila in ipairs(filas) do
        for indice, celda in ipairs(fila.cells) do
            if anchos[indice] then
                local texto = pandoc.utils.stringify(celda.contents)
                local largo = utf8.len(texto) or #texto

                if largo > anchos[indice] then
                    anchos[indice] = largo
                end
            end
        end
    end
end

local function Table(tabla)
    local columnas = #tabla.colspecs

    if columnas == 0 then
        return nil
    end

    local anchos = {}

    for indice = 1, columnas do
        anchos[indice] = 1
    end

    medir(tabla.head.rows, anchos)

    for _, cuerpo in ipairs(tabla.bodies) do
        medir(cuerpo.body, anchos)
    end

    local total = 0

    for indice = 1, columnas do
        if anchos[indice] > ANCHO_MAXIMO then
            anchos[indice] = ANCHO_MAXIMO
        end

        anchos[indice] = anchos[indice] + AIRE
        total = total + anchos[indice]
    end

    for indice = 1, columnas do
        tabla.colspecs[indice][2] = anchos[indice] / total
    end

    return tabla
end

-- Directivas para unir documentos (docs/documentacion-final.md).
--
-- Son comentarios HTML, asi que GitHub no los muestra:
--
--   <!-- incluir 01-instalacion T-28 -->           todos los .md de la carpeta, en orden alfabetico
--   <!-- incluir 05-pruebas/pruebas.md T-27 -->     un archivo
--   <!-- incluir ../README.md "3. Stack" -->        solo el contenido de esa seccion, sin su titulo
--   <!-- indice -->                                 indice en una pagina aparte
--   <!-- salto -->                                  salto de pagina
--
-- Las rutas son relativas al documento que tiene la directiva. Si la carpeta
-- no tiene ningun .md o el archivo no existe, se escribe "Pendiente (T-XX)".
-- Una carpeta no repite los archivos que otra directiva incluye por nombre.
-- Los titulos incluidos bajan de nivel para quedar debajo de la seccion del
-- documento final, y los enlaces e imagenes se ajustan a la nueva ubicacion.
-- Si el documento tiene <!-- indice -->, su primer "# Titulo" no se repite en
-- el cuerpo porque ya es el titulo de la portada.

local FORMATO = 'gfm+tex_math_dollars' -- el mismo --from de compilar-docs.sh

local function normalizar(ruta)
    local partes = {}

    for parte in ruta:gmatch('[^/]+') do
        if parte == '..' and #partes > 0 and partes[#partes] ~= '..' then
            table.remove(partes)
        elseif parte ~= '.' then
            table.insert(partes, parte)
        end
    end

    return table.concat(partes, '/')
end

local function unir(carpeta, ruta)
    if carpeta == '' or carpeta == '.' then
        return normalizar(ruta)
    end

    return normalizar(carpeta .. '/' .. ruta)
end

local function leer(ruta)
    local archivo = io.open(ruta, 'r')

    if not archivo then
        return nil
    end

    local texto = archivo:read('a')
    archivo:close()
    return texto
end

local function listar(carpeta)
    local ok, nombres = pcall(pandoc.system.list_directory, carpeta)

    if not ok then
        return nil
    end

    local archivos = {}

    for _, nombre in ipairs(nombres) do
        if nombre:match('%.md$') then
            table.insert(archivos, unir(carpeta, nombre))
        end
    end

    table.sort(archivos)
    return archivos
end

local function leer_directiva(bloque)
    if bloque.t ~= 'RawBlock' or bloque.format ~= 'html' then
        return nil
    end

    local cuerpo = bloque.text:match('^%s*<!%-%-%s*(.-)%s*%-%->%s*$')

    if not cuerpo then
        return nil
    end

    local orden, resto = cuerpo:match('^(%S+)%s*(.*)$')

    if orden ~= 'incluir' and orden ~= 'indice' and orden ~= 'salto' then
        return nil
    end

    local directiva = { orden = orden, secciones = {} }

    for seccion in resto:gmatch('"([^"]+)"') do
        table.insert(directiva.secciones, seccion)
    end

    resto = resto:gsub('"[^"]*"', ' ')

    for palabra in resto:gmatch('%S+') do
        if palabra:match('^T%-%d+$') then
            directiva.tarea = palabra
        elseif not directiva.ruta then
            directiva.ruta = palabra
        end
    end

    return directiva
end

local function elegir_secciones(bloques, secciones)
    if #secciones == 0 then
        return bloques
    end

    local elegidos = pandoc.Blocks({})
    local nivel = nil

    for _, bloque in ipairs(bloques) do
        if bloque.t == 'Header' then
            local texto = pandoc.utils.stringify(bloque.content)

            if nivel and bloque.level <= nivel then
                nivel = nil
            end

            local abre = false

            for _, seccion in ipairs(secciones) do
                if not nivel and texto:sub(1, #seccion) == seccion then
                    nivel = bloque.level
                    abre = true
                end
            end

            -- El titulo de la seccion elegida lo pone el documento final.
            if abre then
                goto siguiente
            end
        end

        if nivel and bloque.t ~= 'HorizontalRule' then
            elegidos:insert(bloque)
        end

        ::siguiente::
    end

    return elegidos
end

local function aviso_pendiente(tarea, ruta, base)
    local mostrar = unir(base, ruta)
    if not mostrar:match('%.md$') then
        mostrar = mostrar .. '/'
    end

    local texto = '**Pendiente (' .. (tarea or 'sin tarea') .. '):** todavía no hay contenido en `'
        .. mostrar .. '`. Esta sección se completa sola al compilar cuando ese documento exista.'

    return pandoc.BlockQuote(pandoc.read(texto, 'gfm').blocks)
end

local function hacia_base(base, ruta)
    if ruta:sub(1, #base + 1) == base .. '/' then
        return ruta:sub(#base + 2)
    end

    local subir = ''
    for _ in base:gmatch('[^/]+') do
        subir = subir .. '../'
    end

    return subir .. ruta
end

local function es_externa(destino)
    return destino:match('^%a[%w+.-]*:') or destino:sub(1, 1) == '/'
end

local function ajustar(pieza, base, anclas)
    local carpeta = pandoc.path.directory(pieza.ruta)
    local prefijo = pieza.prefijo
    local minimo = nil

    pieza.bloques:walk({
        Header = function(titulo)
            if not minimo or titulo.level < minimo then
                minimo = titulo.level
            end
        end,
    })

    local desplazar = minimo and (2 - minimo) or 0

    return pieza.bloques:walk({
        Header = function(titulo)
            titulo.level = math.min(6, math.max(1, titulo.level + desplazar))
            if titulo.identifier ~= '' then
                titulo.identifier = prefijo .. '-' .. titulo.identifier
            end
            return titulo
        end,

        Link = function(enlace)
            local destino = enlace.target

            if destino:sub(1, 1) == '#' then
                enlace.target = '#' .. prefijo .. '-' .. destino:sub(2)
                return enlace
            end

            if es_externa(destino) then
                return nil
            end

            local ruta, fragmento = destino:match('^([^#]*)#?(.*)$')
            local completa = unir(carpeta, ruta)
            local otra = anclas[completa]

            if otra and fragmento ~= '' then
                enlace.target = '#' .. otra.prefijo .. '-' .. fragmento
            elseif otra and otra.primera then
                enlace.target = '#' .. otra.primera
            else
                enlace.target = hacia_base(base, completa)
                    .. (fragmento ~= '' and ('#' .. fragmento) or '')
            end

            return enlace
        end,

        Image = function(imagen)
            if not es_externa(imagen.src) then
                imagen.src = hacia_base(base, unir(carpeta, imagen.src))
            end
            return imagen
        end,
    })
end

local function unir_documentos(documento)
    local origen = (PANDOC_STATE.input_files or {})[1] or '.'
    local base = normalizar(pandoc.path.directory(origen))
    local directivas = {}
    local reservados = {}
    local con_indice = false

    for indice, bloque in ipairs(documento.blocks) do
        local directiva = leer_directiva(bloque)

        if directiva then
            directivas[indice] = directiva

            if directiva.orden == 'indice' then
                con_indice = true
            end

            if directiva.orden == 'incluir' and directiva.ruta then
                local completa = unir(base, directiva.ruta)

                if not listar(completa) then
                    reservados[completa] = true
                end
            end
        end
    end

    if next(directivas) == nil then
        return nil
    end

    -- Primera pasada: leer cada documento incluido.
    local partes = {}
    local anclas = {}

    local function agregar(ruta, secciones)
        local texto = leer(ruta)

        if not texto then
            return false
        end

        local bloques = elegir_secciones(pandoc.read(texto, FORMATO).blocks, secciones)
        local pieza = {
            ruta = ruta,
            bloques = bloques,
            prefijo = ruta:gsub('%.md$', ''):gsub('[^%w]+', '-'):gsub('^%-+', ''):lower(),
        }

        for _, bloque in ipairs(bloques) do
            if bloque.t == 'Header' and bloque.identifier ~= '' then
                pieza.primera = pieza.prefijo .. '-' .. bloque.identifier
                break
            end
        end

        anclas[ruta] = pieza
        table.insert(partes, pieza)
        return true, pieza
    end

    local quitar_titulo = con_indice
    local resultado = {}

    for indice, bloque in ipairs(documento.blocks) do
        local directiva = directivas[indice]

        if not directiva then
            if quitar_titulo and bloque.t == 'Header' and bloque.level == 1 then
                quitar_titulo = false
            else
                table.insert(resultado, bloque)
            end
        elseif directiva.orden == 'indice' then
            table.insert(resultado, pandoc.RawBlock('latex',
                '\\clearpage\n\\renewcommand*\\contentsname{Índice}\n\\tableofcontents\n\\clearpage'))
        elseif directiva.orden == 'salto' then
            table.insert(resultado, pandoc.RawBlock('latex', '\\clearpage'))
        elseif directiva.ruta then
            local completa = unir(base, directiva.ruta)
            local archivos = listar(completa)
            local encontrado = false

            if archivos then
                for _, archivo in ipairs(archivos) do
                    if not reservados[archivo] and not anclas[archivo] then
                        local _, pieza = agregar(archivo, directiva.secciones)
                        table.insert(resultado, pieza)
                    end
                end
                encontrado = #archivos > 0
            else
                local ok, pieza = agregar(completa, directiva.secciones)
                if ok then
                    table.insert(resultado, pieza)
                end
                encontrado = ok
            end

            if not encontrado then
                table.insert(resultado, aviso_pendiente(directiva.tarea, directiva.ruta, base))
            end
        end
    end

    -- Segunda pasada: con todas las anclas conocidas, ajustar y aplanar.
    local bloques = pandoc.Blocks({})

    for _, elemento in ipairs(resultado) do
        if elemento.prefijo then
            bloques:extend(ajustar(elemento, base, anclas))
        else
            bloques:insert(elemento)
        end
    end

    documento.blocks = bloques
    return documento
end

return {
    { Pandoc = unir_documentos },
    { Code = Code, Table = Table },
}
