cmake_minimum_required(VERSION 3.29.2)
project(CheckOBJC OBJC)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/result.cmake"
  "set(CMAKE_OBJC_COMPILER \"${CMAKE_OBJC_COMPILER}\")\n"
  
  )