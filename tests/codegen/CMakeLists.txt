# this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info

arc_test(insn-selector-test
        SOURCES insn-selector.cpp
        LIBS Arc::Arc
)

arc_test(lowering-test
        SOURCES lowering.cpp
        LIBS Arc::Arc
)

arc_test(selection-dag-test
        SOURCES selection-dag.cpp
        LIBS Arc::Arc
)