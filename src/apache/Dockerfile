FROM httpd:2.4
# El puerto sale de APACHE_PORT; httpd expande ${VARIABLE} con el entorno al arrancar y no arranca si falta
RUN sed -i \
        -e 's/^Listen 80$/Listen ${APACHE_PORT}/' \
        -e 's/^#ServerName .*/ServerName localhost/' \
        /usr/local/apache2/conf/httpd.conf \
    && grep -q '^Listen ${APACHE_PORT}$' /usr/local/apache2/conf/httpd.conf
COPY sitio/ /usr/local/apache2/htdocs/
