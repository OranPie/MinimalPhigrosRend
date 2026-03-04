# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-src"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-build"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/tmp"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/src"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
