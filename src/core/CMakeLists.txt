add_library(lcad_core STATIC
    Color.h
    Ids.h
    geometry/Point2D.h
    geometry/BoundingBox.h
    geometry/Entity.h
    geometry/Line.h
    geometry/Line.cpp
    geometry/Circle.h
    geometry/Circle.cpp
    geometry/Arc.h
    geometry/Arc.cpp
    geometry/Polyline.h
    geometry/Polyline.cpp
    geometry/Ellipse.h
    geometry/Ellipse.cpp
    geometry/Spline.h
    geometry/Spline.cpp
    geometry/Text.h
    geometry/Text.cpp
    geometry/MText.h
    geometry/MText.cpp
    geometry/Dimension.h
    geometry/Dimension.cpp
    geometry/HatchPattern.h
    geometry/HatchPattern.cpp
    geometry/Hatch.h
    geometry/Hatch.cpp
    geometry/Leader.h
    geometry/Leader.cpp
    geometry/MLeader.h
    geometry/MLeader.cpp
    geometry/Table.h
    geometry/Table.cpp
    geometry/Insert.h
    geometry/Insert.cpp
    geometry/PolylineOps.h
    geometry/PolylineOps.cpp
    geometry/ModifyOps.h
    geometry/ModifyOps.cpp
    geometry/SnapGeometry.h
    geometry/SnapGeometry.cpp
    geometry/PointEnt.h
    geometry/ConstructionLine.h
    geometry/AttDef.h
    geometry/Intersect.h
    geometry/Intersect.cpp
    geometry/BoundaryTrace.h
    geometry/BoundaryTrace.cpp
    geometry/Image.h
    geometry/Image.cpp
    geometry/PointCloud.h
    geometry/PointCloud.cpp
    geometry/Wire.h
    geometry/Wire.cpp
    geometry/Junction.h
    geometry/NoConnect.h
    geometry/NetLabel.h
    geometry/Track.h
    geometry/Track.cpp
    geometry/Via.h
    schematic/Netlist.h
    schematic/Netlist.cpp
    schematic/Spice.cpp
    schematic/Erc.h
    schematic/Erc.cpp
    schematic/SymbolLibrary.h
    schematic/SymbolLibrary.cpp
    schematic/Sheets.h
    schematic/Sheets.cpp
    schematic/Bom.h
    schematic/Bom.cpp
    pcb/Stackup.h
    pcb/Stackup.cpp
    pcb/NetClass.h
    pcb/NetClass.cpp
    pcb/FootprintGenerator.h
    pcb/FootprintGenerator.cpp
    pcb/Ratsnest.h
    pcb/Ratsnest.cpp
    pcb/Drc.h
    pcb/Drc.cpp
    pcb/GerberWriter.h
    pcb/GerberWriter.cpp
    pcb/SpecctraWriter.h
    pcb/SpecctraWriter.cpp
    pcb/CopperPour.h
    pcb/CopperPour.cpp
    pcb/Autorouter.h
    pcb/Autorouter.cpp
    pcb/LengthTuning.cpp
    electrical/ElectricalLibrary.h
    electrical/ElectricalLibrary.cpp
    electrical/WireNumbering.h
    electrical/WireNumbering.cpp
    electrical/WireList.h
    electrical/WireList.cpp
    pid/PidLibrary.h
    pid/PidLibrary.cpp
    pid/InstrumentTagging.h
    pid/InstrumentTagging.cpp
    civil/SurveyPoints.h
    civil/SurveyPoints.cpp
    civil/Tin.h
    civil/Tin.cpp
    civil/Contours.h
    civil/Contours.cpp
    civil/CutFill.h
    civil/CutFill.cpp
    civil/Alignment.h
    civil/Alignment.cpp
    cam/Toolpath.h
    cam/Toolpath.cpp
    cam/GCodeWriter.h
    cam/GCodeWriter.cpp
    util/Expr.h
    util/Expr.cpp
    util/Script.cpp
    lisp/LispInterpreter.h
    lisp/LispInterpreter.cpp
    sketch/SketchGeometry.h
    sketch/SketchGeometry.cpp
    sketch/LinearSolve.h
    sketch/LinearSolve.cpp
    sketch/ConstraintSolver.h
    sketch/ConstraintSolver.cpp
    document/Layer.h
    document/Layout.h
    document/LineType.h
    document/LineType.cpp
    document/Block.h
    document/Command.h
    document/Command.cpp
    document/Commands.h
    document/Document.h
    document/Document.cpp
    document/ExpressTools.cpp
    document/Fields.h
    document/Fields.cpp
    document/DataExtraction.h
    document/DataExtraction.cpp
    document/SheetSet.h
    document/SheetSet.cpp
    document/DocumentConstraints.h
    document/DocumentConstraints.cpp
    io/DxfColors.h
    io/DxfColors.cpp
    io/DxfWriter.h
    io/DxfWriter.cpp
    io/DxfReader.h
    io/DxfReader.cpp
    io/DwgReader.h
    io/DwgReader.cpp
    io/DwgWriter.h
    io/DwgWriter.cpp
    io/Xref.h
    io/Xref.cpp
    io/Csv.h
    io/Csv.cpp
    io/PointCloudFile.h
    io/PointCloudFile.cpp
    io/Zip.h
    io/Zip.cpp
)

target_include_directories(lcad_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(lcad_core PUBLIC cxx_std_20)

# Optional DWG import via LibreDWG (https://www.gnu.org/software/libredwg/).
# Looked up in the usual prefixes plus ~/.local, where a from-source
# `./configure --prefix=$HOME/.local && make install` lands it.
option(LCAD_ENABLE_DWG "Enable DWG import via LibreDWG if available" ON)
if(LCAD_ENABLE_DWG)
    find_path(LIBREDWG_INCLUDE_DIR dwg_api.h HINTS $ENV{HOME}/.local/include)
    find_library(LIBREDWG_LIBRARY redwg HINTS $ENV{HOME}/.local/lib)
    if(LIBREDWG_INCLUDE_DIR AND LIBREDWG_LIBRARY)
        target_include_directories(lcad_core PRIVATE ${LIBREDWG_INCLUDE_DIR})
        target_link_libraries(lcad_core PUBLIC ${LIBREDWG_LIBRARY})
        target_compile_definitions(lcad_core PRIVATE LCAD_HAS_DWG)
        message(STATUS "DWG import enabled (LibreDWG: ${LIBREDWG_LIBRARY})")
    else()
        message(STATUS "LibreDWG not found; building without DWG import")
    endif()
endif()

# Optional LAZ (compressed LAS) point cloud reading via LASzip
# (https://laszip.org/), using its stable C "laszip_api" interface.
option(LCAD_ENABLE_LASZIP "Enable LAZ point cloud reading via LASzip if available" ON)
if(LCAD_ENABLE_LASZIP)
    find_path(LASZIP_INCLUDE_DIR laszip/laszip_api.h)
    find_library(LASZIP_LIBRARY laszip_api)
    if(LASZIP_INCLUDE_DIR AND LASZIP_LIBRARY)
        target_include_directories(lcad_core PRIVATE ${LASZIP_INCLUDE_DIR})
        target_link_libraries(lcad_core PUBLIC ${LASZIP_LIBRARY})
        target_compile_definitions(lcad_core PRIVATE LCAD_HAS_LASZIP)
        message(STATUS "LAZ point cloud reading enabled (LASzip: ${LASZIP_LIBRARY})")
    else()
        message(STATUS "LASzip not found; building without LAZ point cloud reading")
    endif()
endif()

# Optional 3D modeling core (Phase 2 of the mega-roadmap) via OpenCASCADE
# (OCCT, https://dev.opencascade.org/) -- a real B-rep kernel, not something
# reasonable to write from scratch (unlike this codebase's other
# from-scratch algorithmic tools: ZIP writer, AutoLISP, Delaunay TIN...).
# core3d/ sources are only added to the build when OCCT is actually found,
# so the 2D-only build stays unaffected on a machine without it.
option(LCAD_ENABLE_3D "Enable the 3D modeling core via OpenCASCADE (OCCT) if available" ON)
if(LCAD_ENABLE_3D)
    find_package(OpenCASCADE QUIET)
    if(OpenCASCADE_FOUND)
        target_include_directories(lcad_core PUBLIC ${OpenCASCADE_INCLUDE_DIR})
        # Foundations only need a modest subset of OCCT's ~70 toolkits; later
        # 3D sprints (STEP/IGES, assembly) will extend this list.
        target_link_libraries(lcad_core PUBLIC
            TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo
            TKPrim TKBO TKMesh TKService TKV3d TKOpenGl TKFillet TKOffset
            TKDESTEP TKDEIGES TKXSBase TKHLR)
        target_compile_definitions(lcad_core PUBLIC LCAD_HAS_OCCT)
        target_sources(lcad_core PRIVATE
            core3d/Feature3D.h
            core3d/Document3D.h
            core3d/Document3D.cpp
            core3d/Commands3D.h
            core3d/SketchToFace.h
            core3d/SketchToFace.cpp
            core3d/StepIges.h
            core3d/StepIges.cpp
            core3d/Persistence3D.h
            core3d/Persistence3D.cpp
            core3d/Assembly.h
            core3d/Assembly.cpp
            core3d/TechDraw.h
            core3d/TechDraw.cpp
            core3d/SheetMetal.h
            core3d/SheetMetal.cpp
            core3d/Bim.h
            core3d/Bim.cpp
            core3d/Piping.h
            core3d/Piping.cpp
            core3d/Cam3D.h
            core3d/Cam3D.cpp
            core3d/Fem.h
            core3d/Fem.cpp
            core3d/Pick3D.h
            core3d/Pick3D.cpp
            pcb/Board3D.h
            pcb/Board3D.cpp)
        message(STATUS "3D modeling core enabled (OpenCASCADE ${OpenCASCADE_VERSION})")
    else()
        message(STATUS "OpenCASCADE not found; building without the 3D modeling core")
    endif()
endif()
