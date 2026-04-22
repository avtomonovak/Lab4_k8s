# Многостадийная сборка для маленького образа

# ===== Этап 1: Сборка =====
FROM gcc:11 AS builder

RUN apt-get update && apt-get install -y make && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY fibonacci_metrics.cpp makefile ./

RUN make clean && make

RUN ls -la && file fibonacci_metrics

# ===== Этап 2: Финальный образ =====
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash fibonacci && \
    mkdir -p /app && \
    chown fibonacci:fibonacci /app

COPY --from=builder /build/fibonacci_metrics /usr/local/bin/

RUN chmod 755 /usr/local/bin/fibonacci_metrics

USER fibonacci

WORKDIR /home/fibonacci

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/fibonacci_metrics"]

LABEL maintainer="avtomonovak" \
      version="1.0.0" \
      description="Fibonacci calculator with Prometheus metrics for K8s"