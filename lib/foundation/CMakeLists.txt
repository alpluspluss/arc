# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Foundation SOURCES
        typed-data.cpp
)

target_link_libraries(Foundation PRIVATE Threads::Threads)
