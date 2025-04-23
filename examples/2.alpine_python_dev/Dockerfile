FROM laplace:alpine

RUN apk add python3

RUN python3 -m venv /root/.venv_a
RUN python3 -m venv /root/.venv_b

RUN source /root/.venv_a/bin/activate; pip install numpy; deactivate;
RUN source /root/.venv_b/bin/activate; pip install numpy<2.0.0; deactivate;

ADD ./test.py /root/