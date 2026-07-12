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
    CsvTests.cpp
    PointCloudFileTests.cpp
    ZipTests.cpp
)
target_link_libraries(lcad_core_tests PRIVATE lcad_core Catch2::Catch2WithMain)

# DwgIoTests.cpp digs into raw LibreDWG objects for cases readDwg() can't
# yet reconstruct on its own (e.g. paper-space layouts); mirror lcad_core's
# own (private) LibreDWG detection here rather than exposing it publicly.
if(LCAD_ENABLE_DWG)
    find_path(LIBREDWG_INCLUDE_DIR dwg_api.h HINTS $ENV{HOME}/.local/include)
    find_library(LIBREDWG_LIBRARY redwg HINTS $ENV{HOME}/.local/lib)
    if(LIBREDWG_INCLUDE_DIR AND LIBREDWG_LIBRARY)
        target_include_directories(lcad_core_tests PRIVATE ${LIBREDWG_INCLUDE_DIR})
        target_link_libraries(lcad_core_tests PRIVATE ${LIBREDWG_LIBRARY})
        target_compile_definitions(lcad_core_tests PRIVATE LCAD_HAS_DWG)
    endif()
endif()

include(CTest)
include(Catch)
catch_discover_tests(lcad_core_tests)
