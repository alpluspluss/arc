# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

# Development-focused configurations for Arc.
# This includes sanitizers, debug tools, and other dev conveniences.
#

# dev build options
option(ARC_ENABLE_SANITIZERS "Enable sanitizers in debug builds" OFF)
option(ARC_STRICT_WARNINGS "Treat warnings as errors" OFF)
option(ARC_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ARC_ENABLE_MSAN "Enable MemorySanitizer" OFF)
option(ARC_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ARC_ENABLE_ASAN "Enable AddressSanitizer" OFF)

# @brief: apply dev configurations
macro(arc_config_dev_tools)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose build type" FORCE)
    endif()

    # strict warnings
    if (ARC_STRICT_WARNINGS)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(arc_base_flags "${arc_base_flags} -Werror")
        elseif (CMAKE_COMPILER_IS_GNUCXX)
            set(arc_base_flags "${arc_base_flags} -Werror")
        endif()
        message(STATUS "Warnings treated as errors")
    endif()

    # sanitizers
    set(sanitizer_flags "")
    set(sanitizer_count 0)

    if (ARC_ENABLE_ASAN)
        set(sanitizer_flags "${sanitizer_flags} -fsanitize=address -fno-omit-frame-pointer")
        math(EXPR sanitizer_count "${sanitizer_count} + 1")
        message(STATUS "AddressSanitizer enabled")
    endif()

    if (ARC_ENABLE_TSAN)
        set(sanitizer_flags "${sanitizer_flags} -fsanitize=thread")
        math(EXPR sanitizer_count "${sanitizer_count} + 1")
        message(STATUS "ThreadSanitizer enabled")
    endif()

    if (ARC_ENABLE_MSAN)
        # MemorySanitizer not available on macOS ARM64
        # this maybe fixed in the future, but for now we skip it
        if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            message(WARNING "MemorySanitizer not supported on macOS ARM64 - skipping")
        else()
            set(sanitizer_flags "${sanitizer_flags} -fsanitize=memory -fno-omit-frame-pointer")
            math(EXPR sanitizer_count "${sanitizer_count} + 1")
            message(STATUS "MemorySanitizer enabled")
        endif()
    endif()

    # ASAN + UBSAN
    if (ARC_ENABLE_SANITIZERS AND sanitizer_count EQUAL 0)
        set(sanitizer_flags "${sanitizer_flags} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
        message(STATUS "Default sanitizers enabled; ASAN + UBSAN")
    endif()

    # warn about conflicting sanitizers
    if (sanitizer_count GREATER 1)
        message(WARNING "Multiple memory sanitizers enabled - this may cause issues")
    endif()

    # **always** add sanitizer flags to debug build
    if (sanitizer_flags)
        set(arc_debug_flags "${arc_debug_flags} ${sanitizer_flags}")
        # also need sanitizer flags for linking
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${sanitizer_flags}")
        set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} ${sanitizer_flags}")
    endif()
endmacro()

# @brief: set up debugging helpers
macro(arc_setup_db_helpers)
    # better debugging symbols
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(arc_debug_flags "${arc_debug_flags} -glldb")
        elseif (CMAKE_COMPILER_IS_GNUCXX)
            set(arc_debug_flags "${arc_debug_flags} -ggdb3")
        endif()
    endif()

    # debug-friendly optimizations
    if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        # frame pointers help making better stack traces
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
    endif()
endmacro()

# @brief: Setup environment variables for development
macro(arc_setup_dev_env)
    if (ARC_ENABLE_ASAN OR ARC_ENABLE_SANITIZERS)
        set(ENV{ASAN_OPTIONS} "detect_leaks=1:abort_on_error=1:check_initialization_order=1")
    endif()

    if (ARC_ENABLE_UBSAN)
        set(ENV{UBSAN_OPTIONS} "halt_on_error=1:abort_on_error=1")
    endif()

    if (ARC_ENABLE_TSAN)
        set(ENV{TSAN_OPTIONS} "halt_on_error=1:abort_on_error=1")
    endif()
endmacro()

# @brief: print development configuration
macro(arc_print_dev_config)
    message(STATUS "Arc Development Configuration:")
    message(STATUS "  Sanitizers: ${ARC_ENABLE_SANITIZERS}")
    if (ARC_ENABLE_ASAN)
        message(STATUS "    - AddressSanitizer: ON")
    endif()
    if (ARC_ENABLE_TSAN)
        message(STATUS "    - ThreadSanitizer: ON")
    endif()
    if (ARC_ENABLE_MSAN)
        message(STATUS "    - MemorySanitizer: ON")
    endif()
    if (ARC_ENABLE_UBSAN)
        message(STATUS "    - UBSanitizer: ON")
    endif()
    message(STATUS "  Strict Warnings: ${ARC_STRICT_WARNINGS}")
endmacro()

# @brief: main macro to configure Arc for development
macro(arc_config_dev)
    arc_config_dev_tools()
    arc_setup_db_helpers()
    arc_setup_dev_env()
    arc_print_dev_config()
endmacro()
