# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Transform SOURCES
        constfold.cpp
        cse.cpp
        dce.cpp
)

target_link_libraries(Transform PRIVATE Threads::Threads)
