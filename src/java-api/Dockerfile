FROM maven:3.9-eclipse-temurin-21 AS builder
WORKDIR /src
# Primero solo el pom, para que las dependencias queden en una capa aparte y no se bajen en cada cambio
COPY pom.xml .
RUN mvn -B -q dependency:go-offline
COPY src ./src
# package corre las pruebas de JUnit; si una falla, la imagen no se construye
RUN mvn -B package

FROM eclipse-temurin:21-jre
RUN useradd --system --no-create-home --uid 10003 javaapi
COPY --from=builder /src/target/java-api.jar /opt/java-api/java-api.jar
USER javaapi
CMD ["java", "-jar", "/opt/java-api/java-api.jar"]
