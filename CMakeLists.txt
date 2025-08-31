# μEmacs - Modern Linux Text Editor
# Pure Linux implementation with modern terminal support

cmake_minimum_required(VERSION 3.20)
project(μEmacs 
    VERSION 0.0.23 
    DESCRIPTION "Modern Linux terminal text editor"
    LANGUAGES C
)

# Tuning knob: minimum literal length for BMH fast path
set(BMH_MIN_LEN 5 CACHE STRING "Minimum length for Boyer–Moore–Horspool literal path (default 5)")

# Modern C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type with optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/bin)

# Required Linux headers
include(CheckIncludeFile)
check_include_file(termios.h HAVE_TERMIOS_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(sys/signalfd.h HAVE_SYS_SIGNALFD_H)
check_include_file(sys/eventfd.h HAVE_SYS_EVENTFD_H)
check_include_file(sys/timerfd.h HAVE_SYS_TIMERFD_H)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/μemacs/util
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Core sources - updated paths
set(CORE_SOURCES
    src/core/main.c
    src/core/basic.c
    src/core/buffer.c
    src/core/window.c
    src/core/display.c
    src/core/transactions.c
    src/core/line.c
    src/core/undo.c
    src/core/undo_persist.c
    src/core/keymap.c
    src/core/globals.c
    src/core/cbuf_dispatch.c
    src/core/command_hooks.c
    src/core/display_matrix.c
    src/core/events.c
    src/core/gapbuffer.c
    src/core/window_hash.c
    src/core/plugin.c
    src/core/sample_plugin.c
)

# Text processing
set(TEXT_SOURCES
    src/text/region.c
    src/text/search.c
    src/text/isearch.c
    src/text/word.c
    src/text/random.c
    src/text/boyer_moore.c
    $<$<BOOL:${ENABLE_SEARCH_NFA}>:src/text/nfa.c>
)

# I/O and file handling
set(IO_SOURCES
    src/io/file.c
    src/io/fileio.c
    src/io/input.c
    src/io/lock.c
    src/io/pklock.c
    src/io/crypt.c
)

# Configuration and commands
set(CONFIG_SOURCES
    src/config/bind.c
    src/config/names.c
    src/config/exec.c
    src/config/eval.c
)

# Linux platform specific
set(PLATFORM_SOURCES
    src/platform/spawn.c
    src/platform/linux-modern.c
)

# Terminal handling - Linux only
set(TERMINAL_SOURCES
    src/terminal/drivers/posix.c
    src/terminal/drivers/termcap.c
    src/terminal/modern-term.c
    src/terminal/capability.c
    src/terminal/display_ops.c
    src/terminal/winsize.c
    src/terminal/atomic_terminal.c
)

# Utilities - Phase 1 modernized utilities
set(UTIL_SOURCES
    src/util/util.c
    src/util/wrapper.c
    src/util/usage.c
    src/util/version.c
    src/util/utf8.c
    src/util/display_width.c
    # Phase 1: Foundation utilities
    src/util/memory.c
    src/util/string.c
    src/util/error.c
    src/util/file.c
    # Phase 5: Performance monitoring
    src/util/profiler.c
    # Phase 3: Security string operations
    src/util/string_enhanced.c
    src/util/string_missing.c
    # Status line integrations (async Git)
    src/util/git_status.c
    # Phase 5: Pattern consolidation
    src/util/buffer_utils.c
)

# All sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${TEXT_SOURCES}
    ${IO_SOURCES}
    ${CONFIG_SOURCES}
    ${PLATFORM_SOURCES}
    ${TERMINAL_SOURCES}
    ${UTIL_SOURCES}
)

# Configuration header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/config.h
)

# Main library
add_library(uemacs STATIC ${ALL_SOURCES})
if(ENABLE_SEARCH_NFA)
  target_compile_definitions(uemacs PRIVATE ENABLE_SEARCH_NFA=1)
endif()
target_compile_definitions(uemacs PRIVATE BMH_MIN_LEN=${BMH_MIN_LEN})

# Main executable
add_executable(muEmacs main.c)
target_link_libraries(muEmacs uemacs ${NCURSES_LIBRARIES} m Threads::Threads) # Link against the static library
set_target_properties(muEmacs PROPERTIES OUTPUT_NAME "μEmacs")

# Apply same compile definitions as the library so main.c builds consistently
target_compile_definitions(muEmacs PRIVATE
    LINUX=1
    UNIX=1
    POSIX=1
    UTF8=1
    COLOR=1
    CRYPT=1
    FILOCK=1
    MODERN=1
    AUTOCONF=1
    HAVE_CONFIG_H=1
    _GNU_SOURCE
    _DEFAULT_SOURCE
)

# Integration test executable (TTY-capable stress testing)
set(TEST_SOURCES
    tests/test_utils.c
    tests/test_phase1_core_ops.c
    tests/test_phase2_navigation.c
    tests/test_phase3_selection.c
    tests/test_phase4_command_validation.c
    tests/test_phase5_advanced_undo_redo.c
    tests/test_keymap.c
    tests/test_api.c
    tests/test_boyer_moore.c
    tests/test_paste.c
    tests/test_undo_deterministic.c
    tests/test_undo_capacity.c
    tests/test_stats.c
    tests/test_utf8.c
    tests/test_undo_advanced.c
    tests/test_search_engines.c
    tests/test_atomic_stats.c
    tests/test_fileio_stub.c
    # 100% coverage completion test suites
    tests/test_terminal_display.c
    tests/test_text_processing.c
    tests/test_external_integrations.c
    tests/test_error_conditions.c
    tests/test_performance_stress.c
    # Phase 1A: Configuration & Scripting Engine
    tests/test_config_engine.c
    # Phase 1B: Security & Encryption Testing
    tests/test_security_encryption.c
    # Phase 2C: Process & Shell Integration Tests
    tests/test_process_shell.c
    # Phase 2D: Transaction & Persistence System Tests
    tests/test_transaction_persistence.c
    # Phase 3E: Advanced Text Operations Tests
    tests/test_advanced_text_ops.c
    # Phase 4H: Platform-Specific Testing
    tests/test_platform_specific.c
    # EXTREME Stress Testing (10X)
    tests/test_extreme_stress.c
)

add_executable(full_integration_test
    tests/full_integration_test.c
    ${TEST_SOURCES}
)
if(ENABLE_SEARCH_NFA)
  target_compile_definitions(full_integration_test PRIVATE ENABLE_SEARCH_NFA=1)
endif()
target_compile_definitions(full_integration_test PRIVATE BMH_MIN_LEN=${BMH_MIN_LEN})

# Simple search microbenchmark
add_executable(bench_search tests/bench/search_bench.c)
target_link_libraries(bench_search uemacs Threads::Threads m)
target_include_directories(bench_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_compile_definitions(bench_search PRIVATE BMH_MIN_LEN=${BMH_MIN_LEN})

add_custom_target(bench
  COMMAND $<TARGET_FILE:bench_search>
  COMMAND $<TARGET_FILE:bench_editor>
  DEPENDS bench_search
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running microbenchmarks (search)"
)

# Editor operations microbenchmark
add_executable(bench_editor tests/bench/editor_bench.c)
target_link_libraries(bench_editor uemacs Threads::Threads m)
target_include_directories(bench_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Compiler definitions
target_compile_definitions(uemacs PRIVATE
    LINUX=1
    UNIX=1
    POSIX=1
    UTF8=1
    COLOR=1
    CRYPT=1
    FILOCK=1
    MODERN=1
    AUTOCONF=1
    HAVE_CONFIG_H=1
    _GNU_SOURCE
    _DEFAULT_SOURCE
)

target_compile_definitions(full_integration_test PRIVATE
    LINUX=1
    UNIX=1
    POSIX=1
    UTF8=1
    COLOR=1
    CRYPT=1
    FILOCK=1
    MODERN=1
    AUTOCONF=1
    HAVE_CONFIG_H=1
    _GNU_SOURCE
    _DEFAULT_SOURCE
)

# Modern compiler flags for safety and optimization  
target_compile_options(uemacs PRIVATE
    # Production warnings - const correctness fixes completed
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wformat-security
    -Wnull-dereference
    -Warray-bounds=2
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    
    # Security hardening
    -fstack-protector-strong
    -fstack-clash-protection
    -fcf-protection
    $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=3>
    
    # Optimizations
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-ggdb>
)

# Linker flags
target_link_options(uemacs PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    $<$<CONFIG:Release>:-s>
)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
find_package(Threads REQUIRED)

# Link libraries - only what modern Linux needs
target_link_libraries(uemacs
    m                           # Math library
    ${NCURSES_LIBRARIES}       # Terminal control (ncursesw)
    Threads::Threads           # pthreads for mutex/signal masking
)

# Link libraries for integration tests  
target_link_libraries(full_integration_test
    m                           # Math library
    uemacs                      # Link against the uemacs static library
    Threads::Threads           # ensure pthreads available in tests too
)

# Help static linker resolve intra-archive deps (e.g., search.o -> nfa.o)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(full_integration_test PRIVATE -Wl,--start-group)
  # end-group will be appended as well to close the group
  set_property(TARGET full_integration_test APPEND_STRING PROPERTY LINK_OPTIONS ";-Wl,--end-group")
endif()

# Add include directories
target_include_directories(uemacs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${NCURSES_INCLUDE_DIRS}
)
target_include_directories(full_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${NCURSES_INCLUDE_DIRS}
)

# Debug build with sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT DISABLE_SANITIZERS)
    target_compile_options(uemacs PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -Werror
        -pedantic-errors
    )
    target_link_options(uemacs PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )

    # Sanitizers for test harness: avoid LeakSanitizer in CI/PTY-restricted envs
    target_compile_options(full_integration_test PRIVATE
        -Werror
        -pedantic-errors
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
    )
    target_link_options(full_integration_test PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )

    target_compile_options(muEmacs PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
    )
    target_link_options(muEmacs PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

# Modern Linux installation following FHS standards
include(GNUInstallDirs)

# Install executable
install(TARGETS muEmacs 
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install configuration files
install(DIRECTORY config/ 
    DESTINATION ${CMAKE_INSTALL_DATADIR}/uemacs
    PATTERN "*.md" EXCLUDE
    PATTERN "*.txt" EXCLUDE
)

# Install documentation
install(FILES README.md 
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME README
)

# Install desktop entry for GUI integration
install(FILES packaging/uemacs.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Install application icon
install(FILES packaging/uemacs.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
)

# Install man page
install(FILES packaging/uemacs.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

# Status message
message(STATUS "μEmacs Linux Build Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")

# Optional: local lint target (not run by default)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_custom_target(lint 
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/cs23_linter.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running C23 safety linter (banned APIs, style, hazards)"
    VERBATIM)
else()
  message(STATUS "Python3 not found; 'lint' target will be unavailable.")
endif()
option(ENABLE_SEARCH_NFA "Enable Thompson NFA MAGIC search engine" ON)
add_custom_target(itests
  COMMAND ${CMAKE_COMMAND} -E env ENABLE_EXPECT=1 LSAN_OPTIONS=detect_leaks=0 ASAN_OPTIONS=detect_leaks=0 $<TARGET_FILE:full_integration_test>
  DEPENDS full_integration_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running interactive expect tests (PTY required)"
)
add_custom_target(api_tests
  COMMAND $<TARGET_FILE:full_integration_test>
  DEPENDS full_integration_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running non-interactive API/integration tests"
)
