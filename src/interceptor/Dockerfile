FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc make libc6-dev libldns-dev libcurl4-openssl-dev libcjson-dev libcriterion-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make interceptor test

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libldns3 libcurl4 libcjson1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN useradd --system --no-create-home --uid 10002 interceptor
COPY --from=builder /src/build/interceptor /usr/local/bin/interceptor
USER interceptor
CMD ["interceptor"]
