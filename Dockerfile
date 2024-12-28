FROM ubuntu:latest

COPY ./build/laplace /
COPY ./adapter/ /adapter/

CMD ["/laplace"]