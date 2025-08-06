# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(callgraph-test
        SOURCES call-graph.cpp
        LIBS Arc::Arc
)

arc_test(tbaa-test
        SOURCES tbaa.cpp
        LIBS Arc::Arc
)
