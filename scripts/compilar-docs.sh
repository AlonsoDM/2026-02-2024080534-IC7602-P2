#!/bin/sh
#
# Compila los Markdown a PDF en docs/pdf/. Recibe que compilar:
#
#   sh scripts/compilar-docs.sh                        todo (docs/ y README.md)
#   sh scripts/compilar-docs.sh all                    lo mismo
#   sh scripts/compilar-docs.sh docs/reportes/P2R1.md  solo ese archivo
#   sh scripts/compilar-docs.sh reportes               solo esa carpeta de docs/
#   sh scripts/compilar-docs.sh README.md              solo el README
#   sh scripts/compilar-docs.sh documentacion-final.md solo el documento final
#
# docs/documentacion-final.md se arma al compilar: docs-filtro.lua reemplaza
# sus directivas <!-- incluir ... --> por los Markdown de cada carpeta.

set -eu

objetivo=${1:-all}

raiz=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
imagen=proxy-cache-docs:3.5

cd "$raiz"

if ! command -v pandoc >/dev/null 2>&1 || ! command -v xelatex >/dev/null 2>&1; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "Hace falta pandoc y xelatex, o Docker, para compilar la documentacion." >&2
        exit 1
    fi

    # pandoc/latex solo se publica para amd64; en ARM (Apple Silicon) Docker lo corre emulado
    if ! docker image inspect "$imagen" >/dev/null 2>&1; then
        echo "Construyendo la imagen $imagen (solo la primera vez)"
        docker build -q --platform linux/amd64 -f scripts/docs.Dockerfile -t "$imagen" scripts >/dev/null
    fi

    montaje=$raiz
    if command -v cygpath >/dev/null 2>&1; then
        montaje=$(cygpath -w "$raiz")
    fi

    proyecto=$(basename "$raiz")

    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' exec docker run --rm --platform linux/amd64 \
        --user "$(id -u):$(id -g)" \
        -v "$montaje:/trabajo/$proyecto" \
        -w "/trabajo/$proyecto" \
        --entrypoint sh \
        "$imagen" scripts/compilar-docs.sh "$objetivo"
fi

salida=docs/pdf
mkdir -p "$salida"

compilados=0
fallidos=''

nombre_pdf() {
    md=$1

    if [ "$md" = "README.md" ]; then
        echo proyecto
        return
    fi

    resto=${md#docs/}
    carpeta=$(dirname "$resto")
    base=$(basename "$resto" .md | sed 's|^\.*||')

    if [ "$carpeta" = "." ]; then
        [ "$base" = "README" ] && echo 00-guia-de-revision || echo "$base"
        return
    fi

    if [ "$base" = "README" ]; then
        echo "$carpeta" | tr '/' '-'
        return
    fi

    if [ "$(find docs -name "$base.md" -not -path 'docs/pdf/*' | wc -l)" -gt 1 ]; then
        echo "$(echo "$carpeta" | tr '/' '-')-$base"
    else
        echo "$base"
    fi
}

rejilla() {
    tex=$1
    awk '
    /\\begin\{longtable\}/ { en = 1 }
    /\\end\{longtable\}/   { en = 0 }
    {
      if (en) {
        sub(/\\begin\{longtable\}\[\]\{@\{\}/, "\\begin{longtable}[]{")
        if ($0 ~ /^[ \t]*>\{/) sub(/>\{/, "|>{")
        sub(/@\{\}\}$/, "|}")
        sub(/\\toprule\\noalign\{\}/, "\\hline")
        sub(/\\midrule\\noalign\{\}/, "")
        sub(/\\bottomrule\\noalign\{\}/, "")
        if ($0 ~ /\\\\$/) $0 = $0 " \\hline"
      }
      print
    }' "$tex" > "$tex.nuevo" && mv "$tex.nuevo" "$tex"
}

compilar() {
    md=$1
    nombre=$(nombre_pdf "$md")
    pdf="$salida/$nombre.pdf"

    titulo=$(sed -n 's/^# \(.*\)/\1/p' "$md" | head -1)
    [ -n "$titulo" ] || titulo=$nombre

    trabajo=$(mktemp -d)

    if ! pandoc "$md" -o "$trabajo/doc.tex" \
        --standalone \
        --from=gfm+tex_math_dollars \
        --columns=40 \
        --lua-filter=scripts/docs-filtro.lua \
        --highlight-style=tango \
        --metadata title="$titulo" \
        -V lang=es \
        -V geometry:margin=2.5cm \
        -V monofont='DejaVu Sans Mono' \
        -V fontsize=10pt \
        -V colorlinks=true \
        2>"$trabajo/pandoc.log"
    then
        echo "  ERROR $md" >&2
        cat "$trabajo/pandoc.log" >&2
        fallidos="$fallidos $md"
        rm -rf "$trabajo"
        return
    fi

    rejilla "$trabajo/doc.tex"

    if (
        cd "$(dirname "$md")"
        xelatex -halt-on-error -interaction=nonstopmode -output-directory="$trabajo" "$trabajo/doc.tex" >"$trabajo/xelatex.log" 2>&1 &&
        xelatex -halt-on-error -interaction=nonstopmode -output-directory="$trabajo" "$trabajo/doc.tex" >"$trabajo/xelatex.log" 2>&1
    ) && [ -f "$trabajo/doc.pdf" ]; then
        mv "$trabajo/doc.pdf" "$pdf"
        echo "  ok    $md -> $pdf"
        compilados=$((compilados + 1))
    else
        echo "  ERROR $md" >&2
        tail -n 30 "$trabajo/xelatex.log" >&2
        fallidos="$fallidos $md"
    fi

    rm -rf "$trabajo"
}

lista_md() {
    ruta=$1

    if [ "$ruta" = all ]; then
        find docs -name '*.md' -not -path 'docs/pdf/*' | sort
        echo README.md
        return
    fi

    ruta=${ruta#./}
    ruta=${ruta%/}

    [ -e "$ruta" ] || ruta=docs/$ruta

    if [ -d "$ruta" ]; then
        find "$ruta" -name '*.md' -not -path 'docs/pdf/*' | sort
    elif [ -f "$ruta" ]; then
        case $ruta in
            *.md) echo "$ruta" ;;
        esac
    fi
}

objetivos=$(lista_md "$objetivo")

if [ -z "$objetivos" ]; then
    echo "No se encontro ningun Markdown para '$objetivo'." >&2
    echo "Use all, la ruta de un .md o una carpeta de docs/." >&2
    exit 1
fi

echo "Compilando la documentacion a $salida/"

for md in $objetivos; do
    compilar "$md"
done

echo ""
echo "$compilados PDF en $salida/"

if [ -n "$fallidos" ]; then
    echo "Fallaron:$fallidos" >&2
    exit 1
fi
