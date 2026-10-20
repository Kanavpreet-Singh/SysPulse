cmake_minimum_required(VERSION 3.20)
project(SysPulse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enforce strict warning flags as required
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Dependencies via FetchContent
include(FetchContent)

# 1. nlohmann/json
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. GoogleTest
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# Prevent GoogleTest from overriding /MD with /MT on Windows MSVC
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ==========================================
# Core Library & Tests
# ==========================================
add_library(syspulse_core STATIC
    core/arena_allocator.cpp
)
target_include_directories(syspulse_core PUBLIC
    core
)

add_executable(core_tests
    core/tests/test_result.cpp
    core/tests/test_arena.cpp
    core/tests/test_ring_buffer.cpp
)
target_link_libraries(core_tests PRIVATE
    syspulse_core
    GTest::gtest_main
)
add_test(NAME CoreTests COMMAND core_tests)

# ==========================================
# Telemetry Agent & Tests
# ==========================================
set(AGENT_SOURCES
    agent/src/config.cpp
    agent/src/cli.cpp
    agent/src/sampler.cpp
    agent/src/writer.cpp
    agent/src/collector_factory.cpp
    agent/src/platform/linux/proc_collector.cpp
)

set(AGENT_PLATFORM_LIBS "")

if (WIN32)
    list(APPEND AGENT_SOURCES
        agent/src/platform/windows/pdh_collector.cpp
        agent/src/platform/windows/win_process_collector.cpp
        agent/src/platform/windows/etw_session.cpp
    )
    list(APPEND AGENT_PLATFORM_LIBS
        pdh
        psapi
        tdh
        advapi32
    )
endif()

add_library(syspulse_agent_lib STATIC
    ${AGENT_SOURCES}
)
target_include_directories(syspulse_agent_lib PUBLIC
    core
    agent/include
)
target_link_libraries(syspulse_agent_lib PUBLIC
    syspulse_core
    nlohmann_json::nlohmann_json
    ${AGENT_PLATFORM_LIBS}
)

add_executable(syspulse_agent
    agent/src/main.cpp
)
target_link_libraries(syspulse_agent PRIVATE
    syspulse_agent_lib
)

add_executable(agent_tests
    agent/tests/test_proc_collector.cpp
)
target_link_libraries(agent_tests PRIVATE
    syspulse_agent_lib
    GTest::gtest_main
)
add_test(NAME AgentTests COMMAND agent_tests)

# ==========================================
# Fault Generator Suite
# ==========================================
add_executable(cpu_spin faultgen/cpu_spin.cpp)
add_executable(mem_leak faultgen/mem_leak.cpp)
add_executable(handle_leak faultgen/handle_leak.cpp)
add_executable(thread_storm faultgen/thread_storm.cpp)
add_executable(crash_test faultgen/crash_test.cpp)
