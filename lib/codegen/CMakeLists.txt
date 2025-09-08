# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_library(Codegen SOURCES
        lowering.cpp
)

target_link_libraries(Codegen PRIVATE Threads::Threads)
