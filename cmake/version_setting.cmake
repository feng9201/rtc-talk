
# 获取cmake执行时间 用于生成版本信息
string(TIMESTAMP COMPILE_TIME %Y%m%d-%H%M%S)

# 大小写的project name
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWERCASE)
string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPERCASE)

# 生成版本信息头文件
configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/Version.h.in
  ${CMAKE_BINARY_DIR}/generated/Version.h
  @ONLY
)

set(VERSION_HEADER "${CMAKE_BINARY_DIR}/generated/Version.h")
set(VERSION_PATH "${CMAKE_BINARY_DIR}/generated")
