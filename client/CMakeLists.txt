cmake_minimum_required(VERSION 3.20)
project(rtc_rk_client VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
find_package(Threads REQUIRED)

option(RTC_ENABLE_SANITIZERS "Enable address/undefined sanitizers on host" OFF)
option(RTC_ENABLE_TSAN "Enable thread sanitizer on host (separate build)" OFF)
if(RTC_ENABLE_SANITIZERS AND RTC_ENABLE_TSAN)
  message(FATAL_ERROR "ASan and TSan must use separate builds")
endif()
if((RTC_ENABLE_SANITIZERS OR RTC_ENABLE_TSAN) AND
   (CMAKE_CROSSCOMPILING OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  message(FATAL_ERROR "This sanitizer preset is supported only on a GNU/Clang host")
endif()

add_library(rtc_foundation STATIC
  libs/domain/session.cpp
  libs/domain/operations.cpp
  libs/video/frame.cpp
  libs/video/budget.cpp
)
target_include_directories(rtc_foundation PUBLIC include)
target_link_libraries(rtc_foundation PUBLIC Threads::Threads)
target_compile_options(rtc_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(RTC_ENABLE_SANITIZERS)
  target_compile_options(rtc_foundation PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(rtc_foundation PUBLIC -fsanitize=address,undefined)
elseif(RTC_ENABLE_TSAN)
  target_compile_options(rtc_foundation PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(rtc_foundation PUBLIC -fsanitize=thread)
endif()

if(BUILD_TESTING)
  foreach(suite session operations video concurrency)
    add_executable(rtc_${suite}_test tests/unit/${suite}_test.cpp)
    target_include_directories(rtc_${suite}_test PRIVATE tests/unit)
    target_compile_options(rtc_${suite}_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(rtc_${suite}_test PRIVATE rtc_foundation)
    add_test(NAME ${suite} COMMAND rtc_${suite}_test)
    set_tests_properties(${suite} PROPERTIES TIMEOUT 20)
  endforeach()
endif()

# Product targets are added when their real backends are delivered. There is
# intentionally no fake executable that reports media success without media.
