# Unit tests for the logic in App/. They build with the computer's own C
# compiler and run on the computer, no board needed:
#
#   cmake -S tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(smart_fan_tests C)

# Unity, a small C test framework that is common in embedded work.
# CMake downloads it the first time.
include(FetchContent)
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.6.0)
FetchContent_MakeAvailable(unity)

enable_testing()

# Optional: build the tests with AddressSanitizer and UndefinedBehaviorSanitizer,
# so out of bounds writes, bad float to integer casts and other undefined
# behaviour stop the test run instead of passing silently:
#
#   cmake -S tests -B build/tests-san -DSANITIZE=ON
#
# This only affects the test programs. The firmware is a separate CMake
# project and never sees these flags.
option(SANITIZE "Build the tests with ASan and UBSan" OFF)
set(SAN_FLAGS -fsanitize=address,undefined,float-cast-overflow
              -fno-sanitize-recover=all -fno-omit-frame-pointer -g)

# One test program per module: test_fan_control and test_link_proto.
foreach(module fan_control link_proto)
  add_executable(test_${module} test_${module}.c ../App/${module}.c)
  target_include_directories(test_${module} PRIVATE ../App)
  target_link_libraries(test_${module} PRIVATE unity)
  target_compile_options(test_${module} PRIVATE -Wall -Wextra -Werror)
  add_test(NAME ${module} COMMAND test_${module})
  if(SANITIZE)
    target_compile_options(test_${module} PRIVATE ${SAN_FLAGS})
    target_link_options(test_${module} PRIVATE ${SAN_FLAGS})
  endif()
endforeach()
