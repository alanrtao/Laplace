FROM ubuntu:latest

COPY ./build/laplace /

CMD ["/laplace"]