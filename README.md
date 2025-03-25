# Laplace

Laplace is primarily meant to be a shell education tool. The motivating principle is to get hands dirty and revert
if anything goes wrong. A Git-like commit history mechanism is used, where each bash command is a commit and its
effects (on files and variables) within the workspace is captured.

## Building
- `git submodule update --init --recursive`
- `mkdir build`, `cd build`, `CC=clang CXX=clang++ cmake ..`, `make -j`

## Usage
> For now, please clear the playground folder before running, since Laplace does not actually handle prior saves very well.
> You may need to run `sudo rm -r playground` since the Docker container has modified the folder as a different user, causing
> it to be write-protected.
- `mkdir playground`, then `docker run --rm -it -p 8008:8008/tcp -v./playground:/__laplace laplace`
- To exit shell, call `Ctrl + D`