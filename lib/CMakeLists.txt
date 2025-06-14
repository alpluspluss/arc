# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

add_subdirectory(support)

# the main Arc library target that includes all components
# and the alias of the main target export name
add_library(arc INTERFACE)
add_library(Arc::Arc ALIAS arc)

target_link_libraries(arc INTERFACE
        Arc::Support
)

if(ARC_INSTALL)
    install(TARGETS arc
            EXPORT ${targets_export_name}
            COMPONENT "${PROJECT_NAME}")
endif()
