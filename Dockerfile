FROM ubuntu:22.04

# Prevents `apt install` from asking questions
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gdb \
    clang \
    git \
    flex \
    bison \
    cmake \
    python3 \
    gdb-multiarch \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]

# Commands:
# docker build --platform linux/arm64 -t sysy-dev .; docker run -it --platform linux/arm64 -v $(pwd):/workspace sysy-dev
