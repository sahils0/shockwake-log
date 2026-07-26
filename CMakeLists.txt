cmake_minimum_required(VERSION 3.14)

project(swl
    VERSION 2.0.1
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
add_library(swl_lib STATIC
    src/config.cpp
    src/ring_buffer.cpp
    src/inotify_watcher.cpp
    src/log_scanner.cpp
    src/webhook_alert.cpp
)

target_include_directories(swl_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(swl_lib PUBLIC
    CURL::libcurl
    Threads::Threads
)

# ──────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────
add_executable(swl src/main.cpp)

target_link_libraries(swl PRIVATE swl_lib)
target_compile_definitions(swl PRIVATE SWL_VERSION="${PROJECT_VERSION}")

# ──────────────────────────────────────────────
# Tests
# ──────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_ring_buffer
        tests/test_ring_buffer.cpp
    )
    target_link_libraries(test_ring_buffer PRIVATE swl_lib)
    add_test(NAME RingBuffer COMMAND test_ring_buffer)

    add_executable(test_log_scanner
        tests/test_log_scanner.cpp
    )
    target_link_libraries(test_log_scanner PRIVATE swl_lib)
    add_test(NAME LogScanner COMMAND test_log_scanner)

    add_executable(test_inotify
        tests/test_inotify.cpp
    )
    target_link_libraries(test_inotify PRIVATE swl_lib)
    add_test(NAME InotifyWatcher COMMAND test_inotify)

    add_executable(test_webhook
        tests/test_webhook.cpp
    )
    target_link_libraries(test_webhook PRIVATE swl_lib)
    add_test(NAME WebhookAlert COMMAND test_webhook)

    add_executable(test_config
        tests/test_config.cpp
    )
    target_link_libraries(test_config PRIVATE swl_lib)
    add_test(NAME Config COMMAND test_config)

    add_executable(test_main
        tests/test_main.cpp
        src/config.cpp
        src/inotify_watcher.cpp
        src/log_scanner.cpp
        src/ring_buffer.cpp
        src/webhook_alert.cpp
    )
    target_include_directories(test_main PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_main PRIVATE
        CURL::libcurl
        Threads::Threads
    )
    target_compile_definitions(test_main PRIVATE SWL_VERSION="${PROJECT_VERSION}")
    add_test(NAME Main COMMAND test_main)
endif()

# ──────────────────────────────────────────────
# Install
# ──────────────────────────────────────────────
install(TARGETS swl
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
        ${CMAKE_INSTALL_PREFIX}/bin/swl
    COMMENT "Uninstalling swl"
)
