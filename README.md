# Laplace

Laplace is primarily meant to be a shell education tool. The motivating principle is to get hands dirty and revert
if anything goes wrong. A Git-like commit history mechanism is used, where each bash command is a commit and its
effects (on files and variables) within the workspace is captured.

The name is inspired by "Laplace's demon" which tracks everything.

## Usage

## Dependencies
- Git does not perfectly implement what is required, namely it always leaves a `.git` folder in the workspace, which is
  not ideal. So instead we use `libgit2` to implement a minimal set of functions needed from Git, such as diffing.
- 