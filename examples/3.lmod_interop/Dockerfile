FROM laplace:bash

ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt \
    --mount=type=cache,target=/var/lib/apt \
    apt update -yq; \
    apt install -yq \
        software-properties-common \
        clang \
        wget \
        autoconf \
        python3 \
        lua5.3 \
        lua-bit32:amd64 \
        lua-posix:amd64 \
        lua-posix-dev \
        liblua5.3-0:amd64 \
        liblua5.3-dev:amd64 \
        tcl \
        tcl-dev \
        tcl8.6 \
        tcl8.6-dev:amd64 \
        libtcl8.6:amd64 \
        bc \
        vim

# no need to propagate this into the work environment, just drop it here
ENV DEBIAN_FRONTEND=

WORKDIR /root
RUN wget https://github.com/TACC/Lmod/archive/refs/tags/8.7.59.tar.gz
RUN tar xzvf 8.7.59.tar.gz
RUN rm 8.7.59.tar.gz
WORKDIR Lmod-8.7.59
RUN ./configure --prefix=/root/apps
RUN make install

WORKDIR /root
RUN rm -r Lmod-8.7.59
RUN mkdir modulefiles
ADD test.c .
