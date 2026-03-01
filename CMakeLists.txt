cmake_minimum_required(VERSION 3.15)

project(Mcache)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# First program (main)
add_executable(MainApp
    src/main.cpp
 "src/hashtable.cpp" "src/avl.cpp" "src/zset.cpp")

target_include_directories(MainApp PUBLIC include)

# Second program (client test)
add_executable(ClientTest
    "test/clientTest.cpp"
)

# Third program (avl test)
add_executable(AvlTest
    "test/avlTest.cpp"
    "src/avl.cpp"
)

target_include_directories(AvlTest PUBLIC include)

# Fourth program (offest test)
add_executable(offestTest
    "test/test_offest.cpp"
    "src/avl.cpp"
)

target_include_directories(offestTest PUBLIC include)