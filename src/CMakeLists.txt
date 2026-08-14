add_executable(rest WIN32
    app/main.cpp
    app/app.manifest
    core/config.cpp
    core/timer.cpp
    shell/tray.cpp
    shell/menu.cpp
    loc/strings.cpp
    notify/hello.cpp
)

target_include_directories(rest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rest PRIVATE UNICODE _UNICODE)
target_compile_options(rest PRIVATE /W4 /Os /utf-8)
target_link_libraries(rest user32 shell32 gdi32 advapi32 dwmapi)
