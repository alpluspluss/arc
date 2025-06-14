# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Support SOURCES
        string-table.cpp
)

target_link_libraries(Support PUBLIC Threads::Threads)
