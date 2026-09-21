cmake_minimum_required(VERSION 3.16)
project(coreactor LANGUAGES CXX)

# 使用现代 C++，并关闭编译器扩展，让代码更接近标准 C++。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 自动收集 src 下的实现文件，包括 main.cpp 和各模块 .cpp。
file(GLOB_RECURSE COREACTOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# 头文件使用 "reactor/..."、"net/..." 等路径，因此把 src 加入包含目录。
add_executable(coreactor ${COREACTOR_SOURCES})
target_include_directories(coreactor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(coreactor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(coreactor PRIVATE pthread)

# 学习用的最小阻塞式 TCP Echo Server，不依赖 coreactor 的任何模块。
add_executable(simple_tcp_server examples/simple_tcp_server.cpp)
target_compile_options(simple_tcp_server PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS coreactor RUNTIME DESTINATION bin)
