# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Analysis SOURCES
        tbaa.cpp
)

target_link_libraries(Analysis PRIVATE Threads::Threads)
