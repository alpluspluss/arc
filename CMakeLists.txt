# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

# Arc project-wide settings.
#
# cmake files in this project can refer to the root source directory as
# ${ARC_SOURCE_DIR} and the root binary directory as ${ARC_BINARY_DIR}.
#
# Arc mainly uses C++ with the standard set to C++26 to benefit from the
# latest standard features for ergonomics such as executions, reflections, and more
# when available.
#

cmake_minimum_required(VERSION 3.30)
project(arc VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ARC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ARC_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# build options
option(ARC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ARC_BUILD_EXAMPLES "Build examples" OFF)
option(ARC_BUILD_TESTS "Build tests" ON)
option(ARC_BUILD_SHARED_LIB "Build shared libraries" OFF)
option(ARC_INSTALL "Install Arc libraries and headers" OFF)

# these commands only run if this is the main project
if(CMAKE_PROJECT_NAME STREQUAL "arc")
    # `BUILD_SHARED_LIBS` is a standard CMake variable
    if(ARC_BUILD_SHARED_LIB)
        set(BUILD_SHARED_LIBS ON)
    else()
        set(BUILD_SHARED_LIBS OFF)
    endif()
else()
    # hide options when Arc is used as subdirectory
    mark_as_advanced(
            ARC_BUILD_BENCHMARKS
            ARC_BUILD_EXAMPLES
            ARC_BUILD_TESTS
            ARC_BUILD_SHARED_LIB
            ARC_INSTALL)
endif()

set(ARC_LIB_TYPE_STR "")
if (ARC_BUILD_SHARED_LIB)
    set(ARC_LIB_TYPE_STR "shared")
else ()
    set(ARC_LIB_TYPE_STR "static")
endif ()

message(STATUS "Building Arc as ${ARC_LIB_TYPE_STR} library")
message(STATUS "Building benchmarks: ${ARC_BUILD_BENCHMARKS}")
message(STATUS "Building examples: ${ARC_BUILD_EXAMPLES}")
message(STATUS "Building tests: ${ARC_BUILD_TESTS}")

# include the public include that will be used by the Arc libraries
include_directories(include)

# installation configuration
if (ARC_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(cmake_package_name Arc CACHE INTERNAL "")
    set(targets_export_name ${cmake_package_name}Targets CACHE INTERNAL "")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE INTERNAL "")
    set(cmake_files_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${cmake_package_name}")
    set(version_file "${generated_dir}/${cmake_package_name}ConfigVersion.cmake")
    set(config_file "${generated_dir}/${cmake_package_name}Config.cmake")
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(arc.utils)
include(arc.dev)

# configure compiler and development tools
configure_tools()
arc_config_dev()
arc_find_dependencies()
arc_print_config()

# process the sub-projects/sub-libraries
add_subdirectory(lib)

# optional components
if(ARC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ARC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ARC_BUILD_BENCHMARKS)
    add_subdirectory(benches)
endif()

# installation rules
if(ARC_INSTALL)
    # generate and install package config files
    write_basic_package_version_file(
            ${version_file}
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY AnyNewerVersion)

    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in"
            "${config_file}"
            INSTALL_DESTINATION ${cmake_files_install_dir})

    install(FILES ${version_file} ${config_file}
            COMPONENT "${PROJECT_NAME}"
            DESTINATION ${cmake_files_install_dir})

    # export targets; this would be separately handled by
    #   individual lib CMakeLists.txt files
    install(EXPORT ${targets_export_name}
            COMPONENT "${PROJECT_NAME}"
            NAMESPACE ${cmake_package_name}::
            DESTINATION ${cmake_files_install_dir})
endif()
