FROM laplace:bash

RUN --mount=type=cache,target=/var/cache/apt \
    --mount=type=cache,target=/var/lib/apt \
    apt update -yq; \
    apt install -yq \
        git \
        gcc \
        make \
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

