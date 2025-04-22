# Existing Work
- On PACE Phoenix, the Zlib LMOD file includes the following
  
    ``` lua
    prepend_path("LIBRARY_PATH","...")
    prepend_path("LD_LIBRARY_PATH","...")
    prepend_path("CPATH","...")
    prepend_path("MANPATH","...")
    prepend_path("PKG_CONFIG_PATH","...")
    prepend_path("CMAKE_PREFIX_PATH","...")
    setenv("ZLIBROOT","...")
    setenv("ZLIB_ROOT","...")
    append_path("MANPATH","")
    ```
- The aim of this example is to show two examples of either using Laplace as an alternative to LMOD or as a level higher than LMOD

# Walkthrough
- Run `docker build -t laplace:lmod .`, this will take a while due to the amount of packages required to set up Lmod.
- Run `docker run --rm -it -p 8008:8008/tcp -v./prepared:/__laplace -v ./modulefiles:/root/modulefiles laplace:lmod`
- 