# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(node-test
        SOURCES node.cpp
        LIBS Arc::Arc
)

arc_test(typed-data-test
        SOURCES typed-data.cpp
        LIBS Arc::Foundation
)
