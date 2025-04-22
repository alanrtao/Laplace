FROM laplace:alpine-bash

# Install Python 2
RUN apk add --no-cache python2 python2-dev py2-pip

# Install Python 3
RUN apk add --no-cache python3 python3-dev py3-pip

# Install virtualenv
RUN pip2 install virtualenv
RUN pip3 install virtualenv

RUN virtualenv -p python2 /root/.venv2 && \
RUN virtualenv -p python3 /root/.venv3