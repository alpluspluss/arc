# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Aarch64 SOURCES
        pcs.cpp
)

target_link_libraries(Aarch64 PRIVATE Threads::Threads)
