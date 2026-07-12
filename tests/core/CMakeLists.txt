include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(lcad_core_tests
    GeometryTests.cpp
    DocumentTests.cpp
    CommandStackTests.cpp
    DxfIoTests.cpp
    DwgIoTests.cpp
    ModifyOpsTests.cpp
    ExprTests.cpp
    LispTests.cpp
)
target_link_libraries(lcad_core_tests PRIVATE lcad_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(lcad_core_tests)
