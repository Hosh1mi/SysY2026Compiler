FROM ubuntu:24.04

# Prevents `apt install` from asking questions
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    flex \
    bison \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]

# Commands:
# docker build --platform linux/amd64 -t sysy-dev .; docker run -it --platform linux/amd64 -v $(pwd):/workspace sysy-dev
