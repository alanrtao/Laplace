FROM ubuntu:noble

RUN apt-get update && apt-get install -yq \
cmake \
build-essential \
gcc \
zlib1g-dev

RUN --mount=type=bind,source=.,target=/src \
    mkdir -p /build && \
    cd /build && \
    cmake /src && \
    make -j && \
    cp laplace /bin/laplace

# /root is the home directory of the default Docker user
COPY ./adapter/bash/ /root/

WORKDIR /root

ENTRYPOINT ["/bin/laplace", "bash"]