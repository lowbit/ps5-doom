FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget unzip make ninja-build python3 git file xxd gdb libcurl4-openssl-dev \
      clang-18 lld-18 llvm-18 libclang-rt-18-dev \
 && rm -rf /var/lib/apt/lists/*
ENV PATH=/usr/lib/llvm-18/bin:$PATH
WORKDIR /src
