# Laplace

Laplace is primarily meant to be a shell education tool. The motivating principle is to get hands dirty and revert
if anything goes wrong. A Git-like commit history mechanism is used, where each bash command is a commit and its
effects (on files and variables) within the workspace is captured.

## Building
- Use CMake to build the library. Doing so will also create a Docker image named `laplace`

## Usage
- `docker run --rm -it laplace`