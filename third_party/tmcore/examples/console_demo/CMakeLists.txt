add_executable(tmcore_console_demo main.cpp)
target_link_libraries(tmcore_console_demo PRIVATE tmcore)

if(MSVC)
    target_compile_options(tmcore_console_demo PRIVATE /utf-8)
endif()
