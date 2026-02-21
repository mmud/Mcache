cmake_minimum_required(VERSION 3.15)

project(Mcache)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# First program (main)
add_executable(MainApp
    src/main.cpp
)

# Second program (client test)
add_executable(ClientTest
    src/clientTest.cpp
)