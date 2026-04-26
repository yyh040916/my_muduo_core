# 设置最低版本和项目名称
cmake_minimum_required(VERSION 3.10)
project(my_muduo_core)

#设置全局的c++标准
set (CMAKE_CXX_STANDARD 11) 
set (CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

add_subdirectory(src)