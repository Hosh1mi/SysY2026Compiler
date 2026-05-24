cmake_minimum_required(VERSION 3.16)
project(SysYCompiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${SRC_DIR}/include)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

bison_target(
    Parser
    ${SRC_DIR}/frontend/parser.y
    ${SRC_DIR}/frontend/parser.cpp
    DEFINES_FILE ${INCLUDE_DIR}/frontend/parser.hpp
)

flex_target(
    Lexer
    ${SRC_DIR}/frontend/lexer.l
    ${SRC_DIR}/frontend/lexer.cpp
)

add_flex_bison_dependency(Lexer Parser)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)

add_executable(compiler
    ${SRC_FILES}
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
)

target_include_directories(compiler PRIVATE ${INCLUDE_DIR})
