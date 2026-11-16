cmake_minimum_required(VERSION 3.16)
project(Feedpoint VERSION 1.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(feedpoint src/feedpoint.c)
target_compile_definitions(feedpoint PRIVATE FEEDPOINT_VERSION="${PROJECT_VERSION}")

if(UNIX)
  target_link_libraries(feedpoint PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
  # Static C runtime: feedpoint.exe needs no Visual C++ redistributable.
  set_property(TARGET feedpoint PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_compile_definitions(feedpoint PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- tests: run against a real dongle; see tests/README.md -------------------

enable_testing()

# The bias tee tests put DC power on the antenna port, so they exist only
# when this is on: only with an antenna or load that can take DC power.
option(FEEDPOINT_TEST_BIAS_TEE
  "Run the bias tee tests, which put DC power on the antenna port" OFF)

add_executable(feedpoint_test tests/feedpoint_test.c)
target_include_directories(feedpoint_test PRIVATE src)
target_compile_definitions(feedpoint_test PRIVATE FEEDPOINT_VERSION="${PROJECT_VERSION}")
add_dependencies(feedpoint_test feedpoint)

if(MSVC)
  target_compile_definitions(feedpoint_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Feedpoint loads the approved driver from beside itself, so put this
# platform's drivers next to the feedpoint the tests run.
if(WIN32)
  set(FEEDPOINT_DRIVERS win-x64)
elseif(APPLE)
  set(FEEDPOINT_DRIVERS osx)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(FEEDPOINT_DRIVERS linux-arm64)
else()
  set(FEEDPOINT_DRIVERS linux-x64)
endif()
add_custom_command(TARGET feedpoint_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/drivers/${FEEDPOINT_DRIVERS} $<TARGET_FILE_DIR:feedpoint>)

# One test per row of the cases table in feedpoint_test.c, by name.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/feedpoint_test.c)
file(STRINGS tests/feedpoint_test.c caseRows REGEX "^    { \"[A-Za-z.]+\", test")
foreach(caseRow ${caseRows})
  string(REGEX REPLACE "^    { \"([A-Za-z.]+)\".*" "\\1" testCase "${caseRow}")
  if(testCase MATCHES "^SetBiasTee" AND NOT FEEDPOINT_TEST_BIAS_TEE)
    continue()
  endif()
  add_test(NAME ${testCase} COMMAND feedpoint_test $<TARGET_FILE:feedpoint> ${testCase})
  set_tests_properties(${testCase} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
endforeach()
