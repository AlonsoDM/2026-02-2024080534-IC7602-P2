FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev libcriterion-dev \
        libcurl4-openssl-dev libcjson-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make zonal-cache test

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4 libcjson1 libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN useradd --system --no-create-home --uid 10001 cache
COPY --from=builder /src/build/zonal-cache /usr/local/bin/zonal-cache
USER cache
CMD ["zonal-cache"]
