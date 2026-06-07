# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-src"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-build"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/tmp"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/src/pybind11-populate-stamp"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/src"
  "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/src/pybind11-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/src/pybind11-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/yanyige/MinimalPhigrosRend/cpp/build_py/_deps/pybind11-subbuild/pybind11-populate-prefix/src/pybind11-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
