FROM ubuntu:latest

COPY ./build/laplace /bin

# /root is the home directory of the default Docker user
COPY ./adapter/bash/ /root/

ENTRYPOINT ["/bin/laplace", "bash"]