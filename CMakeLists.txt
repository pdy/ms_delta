cmake_minimum_required(VERSION 3.19...3.29)

project(MSDelta VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TARGET_NAME ms_delta)

add_executable(${TARGET_NAME}
  src/ms_delta.cpp
)

set_target_properties(
  ${TARGET_NAME}
  PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" # /MT flag
)

target_link_options(
  ${TARGET_NAME} 
  PRIVATE 
    $<$<CONFIG:RELEASE>:/INCREMENTAL:NO>
)

target_link_libraries(
  ${TARGET_NAME}
  PRIVATE
    msdelta
)

target_compile_definitions(
  ${TARGET_NAME}
  PRIVATE
    UNICODE
    _UNICODE
    _WINDOWS
)

install(TARGETS ${TARGET_NAME})
