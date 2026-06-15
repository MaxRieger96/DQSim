FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.local/bin:${PATH}"
ENV UV_PROJECT_ENVIRONMENT=/venv
ENV UV_LINK_MODE=copy

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        binutils \
        ca-certificates \
        curl \
        git \
        build-essential \
        cmake \
        ninja-build \
        g++-15 \
        gcc-15 \
        libboost-dev \
        libboost-context-dev \
        libboost-thread-dev \
        libglpk-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -LsSf https://astral.sh/uv/install.sh | sh

WORKDIR /app
COPY pyproject.toml uv.lock ./
RUN uv sync --frozen --python 3.11.7 --no-install-project

ENTRYPOINT ["uv", "run", "--no-sync", "python", "main.py"]
