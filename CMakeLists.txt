# μEmacs - Modern Linux Text Editor
# Pure Linux implementation with modern terminal support

cmake_minimum_required(VERSION 3.20)
project(μEmacs
    VERSION 3.1.0
    DESCRIPTION "Modern Linux terminal text editor"
    LANGUAGES C
)

# Generate compile_commands.json for IDE/LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Feature toggles (defined early so they affect source lists and targets)
option(UEMACS_DEBUG_LOG "Enable debug logging to /tmp/muemacs_debug.log" OFF)
option(ENABLE_IO_URING "Enable io_uring for async I/O (requires liburing)" OFF)

# io_uring support (optional - for async batch syscalls)
if(ENABLE_IO_URING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(URING liburing)
    if(URING_FOUND)
        message(STATUS "io_uring support ENABLED (Linux 2026 async I/O)")
    else()
        message(WARNING "liburing not found, disabling io_uring support")
        set(ENABLE_IO_URING OFF)
    endif()
endif()


# Modern C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type with optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Required Linux headers
include(CheckIncludeFile)
check_include_file(termios.h HAVE_TERMIOS_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/inotify.h HAVE_SYS_INOTIFY_H)
check_include_file(sys/signalfd.h HAVE_SYS_SIGNALFD_H)
check_include_file(sys/eventfd.h HAVE_SYS_EVENTFD_H)
check_include_file(sys/timerfd.h HAVE_SYS_TIMERFD_H)
check_include_file(linux/io_uring.h HAVE_LINUX_IO_URING_H)
check_include_file(linux/seccomp.h HAVE_LINUX_SECCOMP_H)
check_include_file(linux/landlock.h HAVE_LINUX_LANDLOCK_H)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Core sources - updated paths
set(CORE_SOURCES
    src/core/main.c
    src/core/basic.c
    src/core/buffer.c
    src/core/window.c
    src/core/display.c
    src/core/message_line.c
    src/core/modeline.c
    src/core/edit_batch.c
    src/core/line.c
    src/core/undo.c
    src/core/keymap.c
    src/core/globals.c
    src/core/cbuf_dispatch.c
    src/core/gapbuffer.c
    src/core/gap_storage.c
    src/core/piece_table.c
    src/core/window_hash.c
    src/core/event_bus.c
    src/core/wrap_segments.c
)

# Text processing
set(TEXT_SOURCES
    src/text/region.c
    src/text/search.c
    src/text/isearch.c
    src/text/word.c
    src/text/random.c
)


# I/O and file handling
set(IO_SOURCES
    src/io/file.c
    src/io/file_preload.c
    src/io/fileio.c
    src/io/input.c
    src/io/lock.c
    src/io/pklock.c
    src/io/encrypt.c
    src/io/file_uring.c
)

# Configuration and commands
set(CONFIG_SOURCES
    src/config/bind.c
    src/config/names.c
    src/config/exec.c
    src/config/eval.c
    src/config/writing.c
    src/config/settings.c
)

# Vim/Evil mode infrastructure (c_evil extension provides the commands)
list(APPEND CONFIG_SOURCES
    src/config/vim_core.c
)

# Linux platform specific
set(PLATFORM_SOURCES
    src/platform/spawn.c
    src/platform/linux-modern.c
    src/platform/clipboard.c
    src/platform/event_loop.c
    src/platform/event_loop_epoll.c
    src/platform/timers.c
    src/platform/sandbox.c
)

# io_uring backend (optional, requires kernel headers)
if(HAVE_LINUX_IO_URING_H AND ENABLE_IO_URING)
    list(APPEND PLATFORM_SOURCES src/platform/event_loop_uring.c)
endif()

# Terminal handling - Linux only (pure raw ANSI, no ncurses dependency)
set(TERMINAL_SOURCES
    src/terminal/drivers/ansi.c
    src/terminal/modern-term.c
    src/terminal/capability.c
    src/terminal/cleanup.c
    src/terminal/winsize.c
    src/terminal/atomic_terminal.c
    src/terminal/palette.c
    # Modern terminal integration (2025 Architecture)
    src/terminal/terminal.c
    src/terminal/repl.c
    src/terminal/build.c
    src/terminal/pty.c
    # New terminal layer (2024 Rewrite)
    src/terminal/input_state.c
    # src/terminal/frame_buffer.c  # Removed - file doesn't exist
    src/terminal/signal_handler.c
)

# Utilities - Phase 1 modernized utilities
set(UTIL_SOURCES
    src/util/util.c
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
    # Status line integrations (async Git)
    src/util/git_status.c
    # Phase 5: Pattern consolidation
    src/util/buffer_utils.c
    # Configuration parser
    src/util/tiny_toml.c
    # Debug logging (conditionally compiled)
    src/util/logger.c
)

# UEP - uEmacs Extension Protocol
set(UEP_SOURCES
    src/uep/tiny_json.c
    src/uep/uep.c
    src/uep/uep_providers.c
    src/uep/uep_format.c
    # Layer 2: Script Extensions
    src/uep/uep_scripts.c
    # Layer 3: C23 Extension System
    src/uep/extension.c
    src/uep/extension_api.c
    # Layer 4: Process Isolation (Hybrid IPC for polyglot extensions)
    src/uep/ext_ipc.c
    src/uep/ext_host.c
)

# Syntax Highlighting - Core C23 implementation
set(SYNTAX_SOURCES
    src/syntax/syntax.c
    src/syntax/lexer.c
    src/syntax/languages.c
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
    ${UEP_SOURCES}
    ${SYNTAX_SOURCES}
)

# Configuration headers
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/config.h
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/version.h
)

# Main library
add_library(muemacs_core STATIC ${ALL_SOURCES})

# Debug logging (zero-cost when disabled)
if(UEMACS_DEBUG_LOG)
    target_compile_definitions(muemacs_core PRIVATE UEMACS_DEBUG_LOG=1)
    message(STATUS "Debug logging ENABLED -> /tmp/muemacs_debug.log")
else()
    target_compile_definitions(muemacs_core PRIVATE UEMACS_DEBUG_LOG=0)
endif()

message(STATUS "Vim/Evil mode: EXTENSION (use c_evil extension)")

# Main executable
add_executable(muEmacs main.c)
target_link_libraries(muEmacs muemacs_core m Threads::Threads) # Link against the static library
target_link_options(muEmacs PRIVATE -rdynamic) # Export symbols for backtrace_symbols_fd
set_target_properties(muEmacs PROPERTIES OUTPUT_NAME "μEmacs")

# Extension runner executable (out-of-process extension host)
# This binary loads extensions that require process isolation (Go, Ada, Haskell, etc.)
# and proxies API calls back to the editor via IPC.
add_executable(muemacs-ext-runner
    src/uep/ext_runner.c
    src/uep/ext_ipc.c
    src/util/logger.c
    src/platform/sandbox.c
)
target_include_directories(muemacs-ext-runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(muemacs-ext-runner
    dl                          # dlopen/dlsym for extension loading
    Threads::Threads           # pthreads
)
target_compile_definitions(muemacs-ext-runner PRIVATE
    LINUX=1
    UNIX=1
    POSIX=1
    _GNU_SOURCE
    _DEFAULT_SOURCE
    HAVE_CONFIG_H=1
)
# Share debug logging setting with main editor
if(UEMACS_DEBUG_LOG)
    target_compile_definitions(muemacs-ext-runner PRIVATE UEMACS_DEBUG_LOG=1)
else()
    target_compile_definitions(muemacs-ext-runner PRIVATE UEMACS_DEBUG_LOG=0)
endif()

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
    UEMACS_DEBUG_LOG=$<BOOL:${UEMACS_DEBUG_LOG}>
)

# Integration test executable (TTY-capable stress testing)
set(TEST_SOURCES
    tests/test_utils.c
    tests/test_keymap_defaults.c
    tests/unit/test_io_input.c
    tests/test_mx_command_flow.c
    tests/test_help_prefix_toggle.c
    tests/test_phase1_core_ops.c
    tests/test_phase2_navigation.c
    tests/test_phase3_selection.c
    tests/test_phase4_command_validation.c
    tests/test_phase5_advanced_undo_redo.c
    tests/unit/test_core_keymap.c
    tests/test_api.c
    tests/unit/test_core_undo.c
    tests/test_stats.c
    tests/unit/test_util_utf8.c
    tests/unit/test_core_gapbuffer.c
    tests/unit/test_text_search.c
    tests/test_atomic_stats.c
    tests/test_fileio_stub.c
    tests/test_writing_wrap_poe.c
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
    # Vim Mode, Settings, Soft Wrap, TrueColor tests
    tests/test_vim_mode.c
    tests/test_settings_system.c
    tests/test_soft_wrap_display.c
    tests/test_truecolor_display.c
    # New modular unit tests
    tests/unit/test_core_buffer.c
    tests/unit/test_core_line.c
    tests/unit/test_core_window.c
    tests/unit/test_text_isearch.c
    tests/unit/test_text_region.c
    tests/unit/test_config_settings.c
    tests/unit/test_config_vim.c
    tests/unit/test_io_file.c
    # Terminal integration tests
    tests/unit/test_terminal_pty.c
    # Stub implementations
    tests/unit/test_io_fileio.c
    tests/unit/test_terminal_signal.c
    tests/unit/test_terminal_cleanup.c
    tests/unit/test_terminal_input_state.c
    # Additional modular tests
    tests/unit/test_text_random.c
    tests/unit/test_text_word.c
    tests/unit/test_text_replace.c
    tests/unit/test_text_encrypt.c
    tests/unit/test_config_eval.c
    tests/unit/test_config_exec.c
    tests/unit/test_config_bind.c
    tests/unit/test_config_names.c
    tests/unit/test_util_profiler.c
    tests/unit/test_platform_spawn.c
    tests/unit/test_core_globals.c
    tests/unit/test_atomic_cursor.c
    tests/unit/test_util_memory.c
    tests/unit/test_text_writing.c
    tests/unit/test_core_basic.c
    tests/unit/test_core_window_hash.c
    tests/unit/test_io_lock.c
    tests/unit/test_util_git_status.c
    tests/unit/test_core_display.c
    tests/unit/test_terminal_ansi.c
    tests/unit/test_util_string.c
    tests/unit/test_wrap_segments.c
)

add_executable(full_integration_test
    tests/integration/main_test_suite.c
    ${TEST_SOURCES}
)

# (E4: Standalone test runners deleted - all tests consolidated into full_integration_test)

# Remaining target definitions
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

# Save crash reproduction test
add_executable(save_crash_test tests/test_save_crash.c)
target_link_libraries(save_crash_test muemacs_core Threads::Threads m)
target_include_directories(save_crash_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_compile_definitions(save_crash_test PRIVATE
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

# Simple search microbenchmark
add_executable(bench_search tests/bench/search_bench.c)
target_link_libraries(bench_search muemacs_core Threads::Threads m)
target_include_directories(bench_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

add_custom_target(bench
  COMMAND $<TARGET_FILE:bench_search>
  COMMAND $<TARGET_FILE:bench_keymap>
  COMMAND $<TARGET_FILE:bench_utf8>
  COMMAND $<TARGET_FILE:bench_editor>
  DEPENDS bench_search bench_keymap bench_utf8 bench_editor
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running microbenchmarks (search)"
)

# Editor operations microbenchmark
add_executable(bench_editor tests/bench/editor_bench.c)
target_link_libraries(bench_editor muemacs_core Threads::Threads m)
target_include_directories(bench_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Keymap lookup microbenchmark
add_executable(bench_keymap tests/bench/keymap_bench.c)
target_link_libraries(bench_keymap muemacs_core Threads::Threads m)
target_include_directories(bench_keymap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# UTF-8 encode/decode microbenchmark
add_executable(bench_utf8 tests/bench/utf8_bench.c)
target_link_libraries(bench_utf8 muemacs_core Threads::Threads m)
target_include_directories(bench_utf8 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Compiler definitions
target_compile_definitions(muemacs_core PRIVATE
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
target_compile_options(muemacs_core PRIVATE
    # Production warnings - const correctness fixes completed
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wformat-security
    -Wnull-dereference
    -Warray-bounds=2
    -Wno-unused-parameter  # Legacy fn_t signature requires (int f, int n) everywhere
    -Wno-unused-variable   # Cleanup pass needed to remove these
    -Wno-unused-function   # Static helpers in headers included conditionally

    # C23 safety sweep - type and conversion warnings
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wdouble-promotion
    -Wrestrict
    -Wcast-align

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
target_link_options(muemacs_core PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    $<$<CONFIG:Release>:-s>
)

# Find required libraries (no ncurses - pure raw ANSI terminal driver)
find_package(Threads REQUIRED)

# sublimation: montauk's adaptive sort/search core (C23, pure C ABI).
# One source of truth: linked from the montauk tree, never vendored.
# Build order: montauk builds first (cmake --build <montauk>/build).
set(MONTAUK_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../montauk/build"
    CACHE PATH "montauk build directory containing libsublimation.a")
set(SUBLIMATION_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../montauk/sublimation/src/include")
if(NOT EXISTS "${MONTAUK_BUILD_DIR}/libsublimation.a")
    message(FATAL_ERROR
        "libsublimation.a not found in ${MONTAUK_BUILD_DIR}. "
        "Build montauk first, or point -DMONTAUK_BUILD_DIR at a build "
        "tree that has it.")
endif()
add_library(sublimation STATIC IMPORTED)
set_target_properties(sublimation PROPERTIES
    IMPORTED_LOCATION "${MONTAUK_BUILD_DIR}/libsublimation.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SUBLIMATION_INCLUDE_DIR}")

# Link libraries - only what modern Linux needs (no external deps for terminal)
target_link_libraries(muemacs_core
    sublimation                 # Adaptive sort/search core (montauk)
    m                           # Math library
    Threads::Threads           # pthreads for mutex/signal masking
    util                        # PTY functions (forkpty, openpty)
    dl                          # dlopen/dlsym for extension loading
)

# io_uring linkage (Linux 2026 async I/O)
if(ENABLE_IO_URING AND URING_FOUND)
    target_link_libraries(muemacs_core ${URING_LIBRARIES})
    target_include_directories(muemacs_core PRIVATE ${URING_INCLUDE_DIRS})
    target_compile_definitions(muemacs_core PRIVATE HAVE_LIBURING=1)
endif()

# Raw io_uring support (no liburing, just kernel headers)
if(HAVE_LINUX_IO_URING_H AND ENABLE_IO_URING)
    target_compile_definitions(muemacs_core PRIVATE HAVE_IO_URING=1)
endif()

# Link libraries for integration tests  
target_link_libraries(full_integration_test
    m                           # Math library
    muemacs_core                      # Link against the muemacs_core static library
    Threads::Threads           # ensure pthreads available in tests too
)

# Help static linker resolve intra-archive deps (e.g., intra-archive references)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(full_integration_test PRIVATE -Wl,--start-group)
  # end-group will be appended as well to close the group
  set_property(TARGET full_integration_test APPEND_STRING PROPERTY LINK_OPTIONS ";-Wl,--end-group")
endif()

# Add include directories
target_include_directories(muemacs_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_include_directories(full_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/include/μemacs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Debug build with sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT DISABLE_SANITIZERS)
    target_compile_options(muemacs_core PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -Werror
        -pedantic-errors
    )
    target_link_options(muemacs_core PRIVATE
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

    # Sanitizers for extension runner (out-of-process host)
    # Note: The ext_runner intentionally does NOT use ASan so it can load
    # polyglot extensions (Go, Ada, Haskell) that have incompatible runtimes.
    # This is the whole point of process isolation!
    # target_compile_options(muemacs-ext-runner PRIVATE ...)

    # Sanitizers for save_crash_test
    target_compile_options(save_crash_test PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
    )
    target_link_options(save_crash_test PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )

    # Sanitizers for benchmark targets
    foreach(_bench bench_search bench_keymap bench_utf8 bench_editor)
        target_compile_options(${_bench} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${_bench} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
        )
    endforeach()
endif()

# Modern Linux installation following FHS standards
include(GNUInstallDirs)

# Install executables
install(TARGETS muEmacs muemacs-ext-runner
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Provide an ASCII-friendly command alias alongside the μEmacs binary.
# Create a symlink named `muEmacs` in the install bindir so users can run
# the editor without typing the micro symbol.
install(CODE [[
  # Create ASCII-friendly alias in the install bindir, honoring DESTDIR.
  set(_prefix "${CMAKE_INSTALL_PREFIX}")
  if(_prefix STREQUAL "")
    set(_prefix "/usr/local")
  endif()
  set(_bindir "${_prefix}/bin")
  if(NOT "$ENV{DESTDIR}" STREQUAL "")
    set(_bindir "$ENV{DESTDIR}${_bindir}")
  endif()
  file(MAKE_DIRECTORY "${_bindir}")
  if(EXISTS "${_bindir}/μEmacs")
    # Use relative symlink to avoid embedding absolute paths in packages
    execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "μEmacs" "${_bindir}/muEmacs")
  else()
    message(WARNING "μEmacs binary not found for alias creation in: ${_bindir}")
  endif()
]])

# Install configuration files
install(DIRECTORY config/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/muemacs
    PATTERN "*.md" EXCLUDE
    PATTERN "*.txt" EXCLUDE
)

# Install extension builder script (universal language support)
install(FILES scripts/uep_build.py
    DESTINATION ${CMAKE_INSTALL_DATADIR}/muemacs
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

# Install extension API headers (for building extensions)
install(DIRECTORY include/uep
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Install documentation
install(FILES README.md 
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME README
)

# Install desktop entry for GUI integration
install(FILES packaging/muEmacs.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Install application icon (SVG — scalable, no rasterization needed)
install(FILES packaging/muemacs-icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME muemacs.svg
)

# Install man page
install(FILES packaging/muEmacs.1
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
add_custom_target(itests
  COMMAND ${CMAKE_COMMAND} -E env ENABLE_EXPECT=1 UEMACS_INTERACTIVE=1 LSAN_OPTIONS=detect_leaks=0 ASAN_OPTIONS=detect_leaks=0 $<TARGET_FILE:full_integration_test>
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

# Dedicated heavy stress target (non-interactive, scalable)
add_custom_target(stress
  COMMAND ${CMAKE_COMMAND} -E env EXTREME_STRESS=1 UEMACS_STRESS=1 UEMACS_PHASE_TIMEOUT=1800 $<TARGET_FILE:full_integration_test>
  DEPENDS full_integration_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running extreme non-interactive stress tests (set UEMACS_STRESS to scale)"
)

# TUI tests using pyte terminal emulator (Python)
# Requires: pip install pyte pexpect (or use .venv)
add_custom_target(tui_tests
  COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python ${CMAKE_SOURCE_DIR}/tests/tui/test_highlight.py
  DEPENDS muEmacs
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running TUI tests with pyte terminal emulator"
)

# Packaging via CPack (optional): generates DEB/RPM/TGZ packages
set(CPACK_PACKAGE_NAME "muemacs")
set(CPACK_PACKAGE_VENDOR "μEmacs Project")
set(CPACK_PACKAGE_CONTACT "maintainers@example.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern Linux terminal text editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "TGZ")
if(EXISTS "/etc/debian_version")
  list(APPEND CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "μEmacs Maintainers")
  set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
endif()
if(EXISTS "/etc/redhat-release")
  list(APPEND CPACK_GENERATOR "RPM")
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Editors")
endif()
include(CPack)
