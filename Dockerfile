FROM ubuntu:latest

RUN apt-get update && \
    apt-get install -y socat net-tools && \
    rm -rf /var/lib/apt/lists/*

COPY ./build/laplace /bin
COPY ./adapter/ /adapter/

ENTRYPOINT ["/bin/laplace", "bash"]