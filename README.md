# Laplace

## Building
- `git submodule update --init --recursive`
- Run `export DOCKER_BUILDKIT=1`
- Run `docker build -t laplace:bash -f Dockerfile.bash .`
- Run `docker build -t laplace:zsh -f Dockerfile.zsh .`
- Run `docker build -t laplace:alpine -f Dockerfile.alpine .`
<!-- - `mkdir build`, `cd build`, `cmake ..`, `make -j` -->

## Demos
- See examples at [folder](./examples/)
- Each example uses its own Docker image that is dependent on the above base images.

## Usage
- Laplace saves progress to the `./playground` folder (you can switch to different names in the code below)
- `docker run --rm -it -p 8008:8008/tcp -v./playground:/__laplace laplace:<shell>`
  > Substitute `shell` with either `bash` or `zsh`. Note that currently `zsh` is unimplemented.
  - To delete this progress, do `sudo rm -rf ./playground` in the host
- Once the Docker environment began, open the frontend webpage HTML, you can now interact with the Laplace process by clicking on the
  commit diagram presented on the frontend.
- To exit shell, call `Ctrl + D`