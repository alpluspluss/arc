# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(allocator-test
        SOURCES allocator.cpp
        LIBS Arc::Support
)

arc_test(dump-test
        SOURCES dump.cpp
        LIBS Arc::Arc
)

arc_test(inference-test
        SOURCES inference.cpp
        LIBS Arc::Arc
)

arc_test(slice-test
        SOURCES slice.cpp
        LIBS Arc::Support
)

arc_test(string-table-test
        SOURCES string-table.cpp
        LIBS Arc::Support
)
