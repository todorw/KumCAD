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
    SchematicTests.cpp
    SheetsTests.cpp
    PcbTests.cpp
    ElectricalTests.cpp
    PidTests.cpp
    CivilTests.cpp
    CamTests.cpp
    SketchTests.cpp
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

# PointCloudFileTests.cpp writes a real LAZ fixture via LASzip itself (rather
# than a checked-in binary blob) when LASzip is available; mirror lcad_core's
# own (private) LASzip detection here for that.
if(LCAD_ENABLE_LASZIP)
    find_path(LASZIP_INCLUDE_DIR laszip/laszip_api.h)
    find_library(LASZIP_LIBRARY laszip_api)
    if(LASZIP_INCLUDE_DIR AND LASZIP_LIBRARY)
        target_include_directories(lcad_core_tests PRIVATE ${LASZIP_INCLUDE_DIR})
        target_link_libraries(lcad_core_tests PRIVATE ${LASZIP_LIBRARY})
        target_compile_definitions(lcad_core_tests PRIVATE LCAD_HAS_LASZIP)
    endif()
endif()

# Core3DTests.cpp exercises OpenCASCADE directly (BRepPrimAPI_MakeBox,
# BRepGProp) -- OpenCASCADE_FOUND doesn't cross the add_subdirectory()
# boundary from src/core's scope, so it's re-detected here (cheap, CMake's
# own package cache makes this a no-op the second time).
if(LCAD_ENABLE_3D)
    find_package(OpenCASCADE QUIET)
    if(OpenCASCADE_FOUND)
        target_sources(lcad_core_tests PRIVATE
            Core3DTests.cpp
            SketchToFaceTests.cpp
            SketchFeatureTests.cpp
            StepIgesTests.cpp
            Persistence3DTests.cpp
            AssemblyTests.cpp
            TechDrawTests.cpp
            SheetMetalTests.cpp
            BimTests.cpp
            PipingTests.cpp
            Cam3DTests.cpp
            FemTests.cpp)
    endif()
endif()

include(CTest)
include(Catch)
catch_discover_tests(lcad_core_tests)
