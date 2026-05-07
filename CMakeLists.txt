cmake_minimum_required(VERSION 3.16)
project(SysYCompiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${SRC_DIR}/include)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})

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

add_executable(compiler
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/frontend/ast/ast.cpp
    ${SRC_DIR}/frontend/ast/astPrinter.cpp
    ${SRC_DIR}/mid/ir/ir.cpp
    ${SRC_DIR}/mid/ir/irGen.cpp
    ${SRC_DIR}/frontend/checker/checker.cpp
    ${SRC_DIR}/frontend/checker/errReporter.cpp
    ${SRC_DIR}/backend/arm64/arm64_codegen.cpp
    ${SRC_DIR}/backend/arm64/arm64_context.cpp
    ${SRC_DIR}/mid/opt/deadCodeDelete.cpp
    ${SRC_DIR}/mid/opt/passManager.cpp
    ${SRC_DIR}/mid/opt/arraySimplify.cpp
    ${SRC_DIR}/mid/opt/mem2reg.cpp
    ${SRC_DIR}/mid/opt/tailRecursionEliminate.cpp
    ${SRC_DIR}/mid/opt/localCopyPropagation.cpp
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
)

target_include_directories(compiler PRIVATE
    ${INCLUDE_DIR}
)
