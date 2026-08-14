cmake_minimum_required(VERSION 3.20)
project(rest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_executable(rest WIN32
    src/main.cpp
    src/tray.cpp
    src/rest.manifest
)

target_compile_definitions(rest PRIVATE UNICODE _UNICODE)
target_compile_options(rest PRIVATE /W4 /Os)
target_link_libraries(rest user32 shell32 gdi32 advapi32)
