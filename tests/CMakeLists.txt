# Test suite for CCtPU_MultiXY (TZ §14).
#
# Builds three unit-test executables and registers them with CTest:
#   test_ctpu       — CtPU::apply/applyToVector/calculateFromCalibration
#   test_mathstack  — MathStack ADD/SUB/MUL/DIV + cascade + invalid operand
#   test_pipeline   — integration: synthetic DSOsamples → CtPU → Math → asserts
#
# Each test is a QtTest executable. CTest runs them via `ctest --output-on-failure`.

find_package(Qt5 COMPONENTS Core Test REQUIRED)

# Add the openhantek src tree to the include path so tests can include
# ctpu.h, mathstack.h, etc. without copying them.
include_directories(${CMAKE_SOURCE_DIR}/openhantek/src)

# QtTest requires moc to process the Q_OBJECT test classes; turn on AUTOMOC
# for the test targets so the `test_*.moc` includes resolve.
set(CMAKE_AUTOMOC ON)

# Helper function: declare a QtTest executable + register it with CTest.
function(openhantek_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt5::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

openhantek_add_test(test_ctpu)

# test_mathstack needs MathStack + enums.cpp (Dso::MathOpEnum definition)
add_executable(test_mathstack test_mathstack.cpp
    ${CMAKE_SOURCE_DIR}/openhantek/src/hantekdso/mathstack.cpp
    ${CMAKE_SOURCE_DIR}/openhantek/src/hantekdso/enums.cpp
)
set_target_properties(test_mathstack PROPERTIES AUTOMOC ON)
target_link_libraries(test_mathstack PRIVATE Qt5::Test)
add_test(NAME test_mathstack COMMAND test_mathstack)

# test_pipeline needs MathStack + enums.cpp + scopesettings (header-only)
add_executable(test_pipeline test_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/openhantek/src/hantekdso/mathstack.cpp
    ${CMAKE_SOURCE_DIR}/openhantek/src/hantekdso/enums.cpp
)
set_target_properties(test_pipeline PROPERTIES AUTOMOC ON)
target_link_libraries(test_pipeline PRIVATE Qt5::Test)
add_test(NAME test_pipeline COMMAND test_pipeline)
