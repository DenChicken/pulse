cmake_minimum_required(VERSION 3.16)

project(pulse VERSION 1.0 LANGUAGES CXX)

set(PULSE_WARNINGS
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic>
)

add_library(pulse
    src/agent.cpp
    src/prober.cpp
)

add_library(pulse::pulse ALIAS pulse)

target_include_directories(pulse
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(pulse PUBLIC cxx_std_17)
target_compile_options(pulse PRIVATE ${PULSE_WARNINGS})

find_package(Threads REQUIRED)
target_link_libraries(pulse PUBLIC Threads::Threads)

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()

    add_executable(pulse_test tests/pulse_test.cpp)
    target_link_libraries(pulse_test PRIVATE pulse)
    target_compile_options(pulse_test PRIVATE ${PULSE_WARNINGS})

    add_test(NAME pulse_test COMMAND pulse_test)
endif()
