FROM ubuntu:latest

RUN apt-get update && \
    apt-get install -y zsh && \
    rm -rf /var/lib/apt/lists/*

COPY ./build/laplace /bin

# /root is the home directory of the default Docker user
COPY ./adapter/zsh /root/

ENTRYPOINT ["/bin/laplace", "zsh", "--rcs"]