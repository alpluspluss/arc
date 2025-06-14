# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

# defines functions and macros for building Arc libraries and tests.
#
# note: this file maybe included in other CMake files when Arc is used as a subdirectory.
# so it should only define functions and macros without side effects e.g. setting variables.
#

# @brief: configure compiler and linker flags for Arc
# @usage: call this macro in the main CMakeLists.txt file to set up the compiler flags
#           configure_tools()
macro (configure_tools)
    if (MSVC)
        message(FATAL_ERROR "Arc does not support MSVC. Please use a different standard
                    conforming compiler.")
    endif()

    if (CMAKE_CXX_STANDARD LESS 26)
        message(FATAL_ERROR "Arc strictly requires C++26 or higher. Please set CMAKE_CXX_STANDARD to 26 or higher.")
    endif ()

    # note: maybe enable reflection and execution features in the future when available

    # base flags for all Arc targets
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(arc_base_flags "-Wall -Wextra -Wconversion -Wshadow -Wpedantic")
        set(arc_base_flags "${arc_base_flags} -Wno-c++98-compat -Wno-c++98-compat-pedantic")
        set(arc_base_flags "${arc_base_flags} ${cxx_reflection_flags}")
        set(arc_debug_flags "-O0 -g -fno-omit-frame-pointer")
        set(arc_release_flags "-O3 -DNDEBUG -flto")
    elseif (CMAKE_COMPILER_IS_GNUCXX)
        set(arc_base_flags "-Wall -Wextra -Wconversion -Wshadow -Wpedantic")
        set(arc_base_flags "${arc_base_flags} -Wno-missing-field-initializers")
        set(arc_base_flags "${arc_base_flags} ${cxx_reflection_flags}")
        set(arc_debug_flags "-O0 -g -fno-omit-frame-pointer")
        set(arc_release_flags "-O3 -DNDEBUG -flto")

        if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11.0")
            set(arc_release_flags "${arc_release_flags} -fdevirtualize-at-ltrans")
        endif()
    else ()
        message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Please use GCC or Clang.")
    endif()

    set(arc_default "${arc_base_flags}")
    set(arc_debug "${arc_base_flags} ${arc_debug_flags}")
    set(arc_release "${arc_base_flags} ${arc_release_flags}")

    set(CMAKE_CXX_FLAGS_DEBUG "${arc_debug}")
    set(CMAKE_CXX_FLAGS_RELEASE "${arc_release}")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${arc_base_flags} ${arc_release_flags} -g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${arc_base_flags} -Os -DNDEBUG")
endmacro ()

# @brief: this function create an Arc library with proper configuration
# @usage: arc_library(name SOURCES source1.cpp source2.cpp...)
function(arc_library name)
    cmake_parse_arguments(ARG "SHARED;STATIC" "" "SOURCES" ${ARGN})

    # Determine library type
    if (ARG_SHARED)
        set(lib_type SHARED)
    elseif (ARG_STATIC)
        set(lib_type STATIC)
    else()
        # Use project default
        if (ARC_BUILD_SHARED_LIB)
            set(lib_type SHARED)
        else()
            set(lib_type STATIC)
        endif()
    endif()

    # create the target library
    add_library(${name} ${lib_type} ${ARG_SOURCES})

    # alias target with proper namespace e.g. Arc::Support
    add_library(Arc::${name} ALIAS ${name})

    # set the properties of this target
    set_target_properties(${name} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            CXX_STANDARD 26
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)

    # include directories
    target_include_directories(${name} PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    # compile features
    target_compile_features(${name} PUBLIC cxx_std_26)

    # set up output directories
    set_target_properties(${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

    # add to export set if installing
    if (ARC_INSTALL)
        install(TARGETS ${name}
                EXPORT ${targets_export_name}
                COMPONENT "${PROJECT_NAME}"
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endfunction()

# @brief: create an Arc executable
# @usage: arc_executable(name SOURCES source1.cpp source2.cpp... LIBS lib1 lib2...)
function(arc_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})

    set_target_properties(${name} PROPERTIES
            CXX_STANDARD 26
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # link the libraries if provided
    if (ARG_LIBS)
        target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    endif()

    target_compile_features(${name} PUBLIC cxx_std_26)
endfunction()

# @brief: create an Arc test
# @usage: arc_test(name SOURCES test1.cpp test2.cpp... LIBS lib1 lib2...)
function(arc_test name)
    if (NOT ARC_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    # create the test executable
    arc_executable(${name} SOURCES ${ARG_SOURCES} LIBS ${ARG_LIBS})

    # add to CTest
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)

    # set test properties
    set_tests_properties(${name} PROPERTIES
            TIMEOUT 300  # 5 minutes max per test
            LABELS "unit")
endfunction()

# @brief: create a benchmark executable
# @usage: arc_benchmark(name SOURCES bench1.cpp bench2.cpp... LIBS lib1 lib2...)
function(arc_benchmark name)
    if (NOT ARC_BUILD_BENCHMARKS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    arc_executable(${name} SOURCES ${ARG_SOURCES} LIBS ${ARG_LIBS})

    # benchmarks should be optimized even in debug builds
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${name} PRIVATE -O3)
    endif()
endfunction()

# @brief: install Arc headers
# @usage: arc_install_headers(directory)
function(arc_install_headers directory)
    if (ARC_INSTALL)
        install(DIRECTORY ${directory}/
                COMPONENT "${PROJECT_NAME}"
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                FILES_MATCHING
                PATTERN "*.hpp"
                PATTERN "*.h"
                PATTERN "*.inl")
    endif()
endfunction()

# @brief: helper to find common dependencies
macro(arc_find_dependencies)
    # find threading support
    find_package(Threads REQUIRED)

    # find Google Benchmark if building benchmarks
    if (ARC_BUILD_BENCHMARKS)
        find_package(benchmark QUIET)
        if (NOT benchmark_FOUND)
            message(STATUS "Google Benchmark not found. Please install it to build benchmarks.")
        endif()
    endif()

    # find Google Test if building tests
    if (ARC_BUILD_TESTS)
        find_package(GTest QUIET)
        if (NOT GTest_FOUND)
            message(STATUS "Google Test not found. Please install it to build tests.")
        endif()
    endif()
endmacro()

# @brief: print Arc build configuration
macro(arc_print_config)
    message(STATUS "Arc Configuration:")
    message(STATUS "  Version: ${PROJECT_VERSION}")
    message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
    message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
    message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  Library type: ${ARC_LIB_TYPE_STR}")
    message(STATUS "  Install: ${ARC_INSTALL}")
endmacro()
