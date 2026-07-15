cmake_minimum_required(VERSION 3.14)

project(shockwake-log
    VERSION 1.0.0
    DESCRIPTION "Real-time log anomaly detection and contextual debugging"
    LANGUAGES CXX
)

# ──────────────────────────────────────────────
# Build configuration
# ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ──────────────────────────────────────────────
# Compiler warnings (professional defaults)
# ──────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fsanitize=address)
    link_libraries(sanitizer)
else()
    # -Os: optimize for size (smaller binary, less cache pressure)
    add_compile_options(-Os)

    # Strip unused code at link time
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# ──────────────────────────────────────────────
# Link-Time Optimization (dead code elimination across TUs)
# ──────────────────────────────────────────────
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# ──────────────────────────────────────────────
# Dependencies (prefer static curl to reduce memory)
# ──────────────────────────────────────────────
set(CURL_USE_STATIC_LIBS ON)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# ──────────────────────────────────────────────
# Library (reusable component)
# ──────────────────────────────────────────────
add_library(shockwake_lib STATIC
    src/config.cpp
    src/ring_buffer.cpp
    src/inotify_watcher.cpp
    src/log_scanner.cpp
    src/webhook_alert.cpp
)

target_include_directories(shockwake_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(shockwake_lib PUBLIC
    CURL::libcurl
    Threads::Threads
)

# ──────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────
add_executable(shockwake-log src/main.cpp)

target_link_libraries(shockwake-log PRIVATE shockwake_lib)

# ──────────────────────────────────────────────
# Tests
# ──────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_ring_buffer
        tests/test_ring_buffer.cpp
    )
    target_link_libraries(test_ring_buffer PRIVATE shockwake_lib)
    add_test(NAME RingBuffer COMMAND test_ring_buffer)

    add_executable(test_log_scanner
        tests/test_log_scanner.cpp
    )
    target_link_libraries(test_log_scanner PRIVATE shockwake_lib)
    add_test(NAME LogScanner COMMAND test_log_scanner)

    add_executable(test_inotify
        tests/test_inotify.cpp
    )
    target_link_libraries(test_inotify PRIVATE shockwake_lib)
    add_test(NAME InotifyWatcher COMMAND test_inotify)

    add_executable(test_webhook
        tests/test_webhook.cpp
    )
    target_link_libraries(test_webhook PRIVATE shockwake_lib)
    add_test(NAME WebhookAlert COMMAND test_webhook)
endif()

# ──────────────────────────────────────────────
# Install
# ──────────────────────────────────────────────
install(TARGETS shockwake-log
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ──────────────────────────────────────────────
# Uninstall
# ──────────────────────────────────────────────
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -E remove
        ${CMAKE_INSTALL_PREFIX}/bin/shockwake-log
    COMMENT "Uninstalling shockwake-log"
)
