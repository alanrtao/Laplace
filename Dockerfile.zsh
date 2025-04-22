FROM laplace:bash

RUN apt-get update && \
    apt-get install -y zsh && \
    rm -rf /var/lib/apt/lists/*

# /root is the home directory of the default Docker user
COPY ./adapter/zsh /root/

ENTRYPOINT ["/bin/laplace", "zsh", "--rcs"]