cmake_minimum_required(VERSION 3.20)
project(ClassTimeEngine LANGUAGES CXX)

# 该工程只构建“可独立验证”的作息引擎与测试，不参与 WinUI 3 打包。
# WinUI 3 应用工程见 windows/ClassTime.vcxproj。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(classtime_engine STATIC
  engine/src/json.cpp
  engine/src/schedule.cpp
  engine/src/time_engine.cpp
  engine/src/config.cpp
  engine/src/edit_ops.cpp
)

target_include_directories(classtime_engine PUBLIC engine/include)

if(MSVC)
  target_compile_options(classtime_engine PRIVATE /utf-8 /W4 /permissive-)
  target_compile_options(classtime_engine PUBLIC /utf-8)
else()
  target_compile_options(classtime_engine PRIVATE -Wall -Wextra)
endif()

add_executable(engine_tests tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE classtime_engine)

enable_testing()
add_test(NAME engine_tests COMMAND engine_tests ${CMAKE_CURRENT_SOURCE_DIR}/..)
