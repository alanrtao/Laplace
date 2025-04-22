# Laplace

Laplace is primarily meant to be a shell education tool. The motivating principle is to get hands dirty and revert
if anything goes wrong. A Git-like commit history mechanism is used, where each bash command is a commit and its
effects (on files and variables) within the workspace is captured.

## Building
- `git submodule update --init --recursive`
- `mkdir build`, `cd build`, `CC=clang CXX=clang++ cmake ..`, `make -j`

## Usage
- Laplace saves progress to the `./playground` folder (you can switch to different names in the code below)
- `docker run --rm -it -p 8008:8008/tcp -v./playground:/__laplace laplace:<shell>`
  > Substitute `shell` with either `bash` or `zsh`. Note that currently `zsh` is unimplemented.
  - To delete this progress, do `sudo rm -rf ./playground` in the host
- Once the Docker environment began, open the frontend webpage HTML, you can now interact with the Laplace process by clicking on the
  commit diagram presented on the frontend.
- To exit shell, call `Ctrl + D`