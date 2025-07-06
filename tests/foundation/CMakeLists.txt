# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(builder-test
        SOURCES builder.cpp
        LIBS Arc::Arc
)

arc_test(module-test
        SOURCES module.cpp
        LIBS Arc::Arc
)

arc_test(node-test
        SOURCES node.cpp
        LIBS Arc::Arc
)

arc_test(pm-test
        SOURCES pass-manager.cpp
        LIBS Arc::Arc
)

arc_test(region-test
        SOURCES region.cpp
        LIBS Arc::Arc
)

arc_test(typed-data-test
        SOURCES typed-data.cpp
        LIBS Arc::Arc
)
