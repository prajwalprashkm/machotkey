cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

project(MachotkeyUserBuild LANGUAGES C CXX OBJCXX)

# Open-source default entrypoint: run the user flow from repo root.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/user/CMakeLists.txt")
