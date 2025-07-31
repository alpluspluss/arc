# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(constfold-test
        SOURCES constfold.cpp
        LIBS Arc::Arc
)

arc_test(dce-test
        SOURCES dce.cpp
        LIBS Arc::Arc
)

arc_test(cse-test
        SOURCES cse.cpp
        LIBS Arc::Arc
)
