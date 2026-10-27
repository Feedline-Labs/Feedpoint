cmake_minimum_required(VERSION 3.16)
project(Feedpoint C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(feedpoint src/feedpoint.c)

if(UNIX)
  target_link_libraries(feedpoint PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
  # Static C runtime: feedpoint.exe needs no Visual C++ redistributable.
  set_property(TARGET feedpoint PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_compile_definitions(feedpoint PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
