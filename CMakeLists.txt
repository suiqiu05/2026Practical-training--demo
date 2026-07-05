cmake_minimum_required(VERSION 3.16)
project(email_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认 MySQL 安装根目录（你提供的路径）
set(MYSQL_ROOT "C:/Program Files/MySQL/MySQL Connector C++ 9.7" CACHE PATH "MySQL install root (default)")

# 查找 mysql.h
find_path(MYSQL_INCLUDE
  NAMES mysql.h
  PATHS
    "${MYSQL_ROOT}/include"
    "${MYSQL_ROOT}/include/cppconn"
    "${MYSQL_ROOT}/include/mysql"
    "${MYSQL_ROOT}/mysql/lib/include"
    "C:/Program Files/MySQL/MySQL Server 8.0/include"
    "C:/Program Files/MySQL/Connector C 8.0/include"
)

# 尝试查找多个可能的库名（C API / Connector-C / Connector-C++）
find_library(MYSQL_LIB
  NAMES libmysql mysqlclient libmysqlclient mysqlcppconn mysqlcppconn8
  PATHS
    "${MYSQL_ROOT}/lib"
    "${MYSQL_ROOT}/lib64"
    "${MYSQL_ROOT}/lib/opt"
    "C:/Program Files/MySQL/MySQL Server 8.0/lib"
    "C:/Program Files/MySQL/Connector C 8.0/lib"
)

# 允许用户通过 -DMYSQL_ROOT=... 覆盖
if(NOT MYSQL_INCLUDE)
  find_path(MYSQL_INCLUDE NAMES mysql.h PATHS "${MYSQL_ROOT}/include" "${MYSQL_ROOT}/include/mysql")
endif()
if(NOT MYSQL_LIB)
  find_library(MYSQL_LIB NAMES libmysql mysqlclient libmysqlclient mysqlcppconn mysqlcppconn8 PATHS "${MYSQL_ROOT}/lib" "${MYSQL_ROOT}/lib64")
endif()

if(NOT MYSQL_INCLUDE)
  message(FATAL_ERROR "找不到 mysql.h。请确认已安装 MySQL/Connector-C 或 Connector-C++，或使用 -DMYSQL_ROOT 指定安装目录（例如 -DMYSQL_ROOT=\"C:/Program Files/MySQL/MySQL Connector C++ 9.7\"）。")
endif()

if(NOT MYSQL_LIB)
  message(FATAL_ERROR "找不到 libmysql/libmysql.lib 或 mysqlcppconn 等链接库。请确认库已安装且位数（x86/x64）与生成器一致。")
endif()

message(STATUS "Found MySQL include: ${MYSQL_INCLUDE}")
message(STATUS "Found MySQL lib: ${MYSQL_LIB}")

# 源文件
add_executable(server
  main.cpp
  httplib.h
  json.hpp
)

target_include_directories(server PRIVATE ${MYSQL_INCLUDE})
target_link_libraries(server PRIVATE "D:/mysql8/mysql-8.0.46-winx64/lib/libmysql.lib")

# 运行时：若是动态库请确保 libmysql.dll 在 PATH 或可执行文件目录