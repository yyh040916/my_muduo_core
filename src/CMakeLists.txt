# 手敲学习时：先把「可运行的小目标」编出来，再逐步把 .cc 挪进库目标里。

# 方式 A（当前）：只有一个 demo 可执行文件，头文件在 ../include
add_executable(demo main.cpp)
target_include_directories(demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(demo PRIVATE -Wall -Wextra)
target_link_libraries(demo PRIVATE ${MY_MUDUO_LIBS})
