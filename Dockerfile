FROM ubuntu:latest

RUN apt-get update && \
    apt-get install -y zsh && \
    rm -rf /var/lib/apt/lists/*

COPY ./build/laplace /bin
COPY ./adapter/ /workspace

ENTRYPOINT ["/bin/laplace", "zsh"]