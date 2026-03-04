# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-src"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-build"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/tmp"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/src/sdl3-populate-stamp"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/src"
  "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/src/sdl3-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/src/sdl3-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/MinimalPhigrosRend/cpp/build_bgfx/_deps/sdl3-subbuild/sdl3-populate-prefix/src/sdl3-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
