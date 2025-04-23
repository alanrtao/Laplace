# Alpine Python Environment Demo
- This demo originally planned to showcase interop capacity between Laplace and Python Virtual Environments. Due to time constraints, this has not been completed.
- You can still build and run the image with
  - `docker build -t laplace:python .`
  - `docker run --rm -it -p 8008:8008/tcp -v./prepared:/__laplace laplace:python`
- This would just be the normal Alpine image, but with venv data folders at `.venv_a` and `.venv_b` representing the two environments.
- It is expected for Laplace to work fine switching between the environments and keeping track, but testing has not been done.