find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets PrintSupport)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# WIN32 has no effect except when actually targeting Windows (e.g. the mingw
# cross-compile in packaging/windows/) -- it builds a GUI subsystem exe there
# instead of a console one, so kumcad.exe doesn't pop a console window.
add_executable(kumcad WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    DrawingView.h
    DrawingView.cpp
    EntityPainter.h
    EntityPainter.cpp
    CommandLine.h
    CommandLine.cpp
    CommandDispatcher.h
    CommandDispatcher.cpp
    IconFactory.h
    IconFactory.cpp
    RecentFiles.h
    RecentFiles.cpp
    WelcomeScreen.h
    WelcomeScreen.cpp
    CommandAliases.h
    CommandAliases.cpp
    CommandAliasDialog.h
    CommandAliasDialog.cpp
    LayerPanel.h
    LayerPanel.cpp
    LayerStatesDialog.h
    LayerStatesDialog.cpp
    PlotStyleDialog.h
    PlotStyleDialog.cpp
    PrintRenderer.h
    PrintRenderer.cpp
    PropertiesPanel.h
    PropertiesPanel.cpp
    ToolPalette.h
    ToolPalette.cpp
    SheetSetPanel.h
    SheetSetPanel.cpp
    DesignCenterPanel.h
    DesignCenterPanel.cpp
    MarkupSetPanel.h
    MarkupSetPanel.cpp
    commands/DrawCommand.h
    commands/LineCommand.h
    commands/LineCommand.cpp
    commands/AnnoScaleCommand.h
    commands/LtScaleCommand.h
    commands/CircleCommand.h
    commands/CircleCommand.cpp
    commands/ArcCommand.h
    commands/ArcCommand.cpp
    commands/PolylineCommand.h
    commands/PolylineCommand.cpp
    commands/PeditCommand.h
    commands/PeditCommand.cpp
    commands/EllipseCommand.h
    commands/EllipseCommand.cpp
    commands/SplineCommand.h
    commands/SplineCommand.cpp
    commands/TextCommand.h
    commands/TextCommand.cpp
    commands/MTextCommand.h
    commands/MTextCommand.cpp
    commands/FieldCommand.h
    commands/FieldCommand.cpp
    commands/DataExtractionCommand.h
    commands/DataExtractionCommand.cpp
    commands/MviewCommand.h
    commands/MviewCommand.cpp
    commands/VpScaleCommand.h
    commands/MoveCommand.h
    commands/MoveCommand.cpp
    commands/StretchCommand.h
    commands/StretchCommand.cpp
    commands/LengthenCommand.h
    commands/LengthenCommand.cpp
    commands/BreakCommand.h
    commands/BreakCommand.cpp
    commands/AlignCommand.h
    commands/AlignCommand.cpp
    commands/OsnapCommand.h
    commands/OsnapCommand.cpp
    commands/PolarAngCommand.h
    commands/StyleCommand.h
    commands/StyleCommand.cpp
    commands/TableCommand.h
    commands/TableCommand.cpp
    commands/TableEditCommand.h
    commands/TableEditCommand.cpp
    commands/LayoutCommand.h
    commands/LayoutCommand.cpp
    commands/PageSetupCommand.h
    commands/PageSetupCommand.cpp
    commands/XrefCommand.h
    commands/XrefCommand.cpp
    commands/PointCloudAttachCommand.h
    commands/PointCloudAttachCommand.cpp
    commands/PointCommands.h
    commands/PointCommands.cpp
    commands/XlineCommand.h
    commands/GradientCommand.h
    commands/GradientCommand.cpp
    commands/HatchBoundary.h
    commands/HatchBoundary.cpp
    commands/GroupCommand.h
    commands/MatchPropCommand.h
    commands/LweightCommand.h
    commands/IdCommand.h
    commands/ImageAttachCommand.h
    commands/ImageAttachCommand.cpp
    commands/AttDefCommand.h
    commands/AttDefCommand.cpp
    commands/CopyCommand.h
    commands/CopyCommand.cpp
    commands/RotateCommand.h
    commands/RotateCommand.cpp
    commands/ScaleCommand.h
    commands/ScaleCommand.cpp
    commands/RectangCommand.h
    commands/RectangCommand.cpp
    commands/MirrorCommand.h
    commands/MirrorCommand.cpp
    commands/OffsetCommand.h
    commands/OffsetCommand.cpp
    commands/DistCommand.h
    commands/DistCommand.cpp
    commands/TrimCommand.h
    commands/TrimCommand.cpp
    commands/ExtendCommand.h
    commands/ExtendCommand.cpp
    commands/GeoLocationCommand.h
    commands/GeoLocationCommand.cpp
    commands/FilletCommand.h
    commands/FilletCommand.cpp
    commands/FindCommand.h
    commands/FindCommand.cpp
    commands/QSelectCommand.h
    commands/QSelectCommand.cpp
    commands/QuickCalcCommand.h
    commands/QuickCalcCommand.cpp
    commands/HatchCommand.h
    commands/HatchCommand.cpp
    commands/DimCommand.h
    commands/DimCommand.cpp
    commands/DimRadialCommand.h
    commands/DimRadialCommand.cpp
    commands/ClipboardPasteCommand.h
    commands/ClipboardPasteCommand.cpp
    commands/DataLinkCommand.h
    commands/DataLinkCommand.cpp
    commands/LayerStateCommand.h
    commands/LayerStateCommand.cpp
    commands/TCaseCommand.h
    commands/TCaseCommand.cpp
    commands/DimAngularCommand.h
    commands/DimAngularCommand.cpp
    commands/DimStyleCommand.h
    commands/DimStyleCommand.cpp
    commands/AreaCommand.h
    commands/AreaCommand.cpp
    commands/ArrayCommand.h
    commands/ArrayCommand.cpp
    commands/BlockCommand.h
    commands/BlockCommand.cpp
    commands/BlockParamCommand.h
    commands/BlockParamCommand.cpp
    commands/InsertCommand.h
    commands/InsertCommand.cpp
    commands/LeaderCommand.h
    commands/LeaderCommand.cpp
    commands/MLeaderCommand.h
    commands/MLeaderCommand.cpp
    commands/MLeaderStyleCommand.h
    commands/MLeaderStyleCommand.cpp
    commands/WireCommand.h
    commands/WireCommand.cpp
    commands/SchematicCommands.h
    commands/SchematicCommands.cpp
    commands/PcbCommands.h
    commands/PcbCommands.cpp
    commands/ParametricCommands.h
    commands/CivilCommands.h
    commands/CivilCommands.cpp
    commands/CamCommands.h
    commands/CamCommands.cpp
)

target_include_directories(kumcad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kumcad PRIVATE lcad_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::PrintSupport)

if(MINGW)
    # Qt's prebuilt static EntryPoint (WinMain wrapper, needed since kumcad is
    # a WIN32 executable) references __argc as a plain imported data symbol,
    # which only the "-os" msvcrt import lib provides -- the generic
    # libmsvcrt.a this toolchain links by default only exposes the
    # __p___argc() accessor. Qt's own finalizer appends its EntryPoint
    # dependency to the link line *after* anything this file's
    # target_link_libraries() calls add, so a plain "-lmsvcrt-os" here would
    # sit before it on the command line and never get rescanned by ld once
    # EntryPoint's undefined reference shows up later. Wrapping the whole
    # <LINK_LIBRARIES> set in --start-group/--end-group makes ld keep
    # rescanning until everything resolves, regardless of order.
    string(REPLACE "<LINK_LIBRARIES>" "-Wl,--start-group <LINK_LIBRARIES> -lmsvcrt-os -Wl,--end-group"
           CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")
endif()

# Optional PDF underlay (PDFATTACH) via Poppler's Qt6 binding.
option(LCAD_ENABLE_PDF "Enable PDF underlay rasterization via Poppler-Qt6 if available" ON)
if(LCAD_ENABLE_PDF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(POPPLER_QT6 QUIET poppler-qt6)
    endif()
    if(POPPLER_QT6_FOUND)
        target_include_directories(kumcad PRIVATE ${POPPLER_QT6_INCLUDE_DIRS})
        target_link_libraries(kumcad PRIVATE ${POPPLER_QT6_LIBRARIES})
        target_compile_definitions(kumcad PRIVATE LCAD_HAS_PDF)
        message(STATUS "PDF underlay enabled (Poppler-Qt6)")
    else()
        message(STATUS "Poppler-Qt6 not found; building without PDF underlay support")
    endif()
endif()

# Viewport3D/Window3D (3D modeling core UI) need OCCT headers directly, so
# they're only compiled when it's available -- OpenCASCADE_FOUND doesn't
# cross the add_subdirectory() boundary from src/core's scope, so it's
# re-detected here (cheap, CMake's own package cache makes this a no-op).
if(LCAD_ENABLE_3D)
    find_package(OpenCASCADE QUIET)
    if(OpenCASCADE_FOUND)
        target_sources(kumcad PRIVATE
            Viewport3D.h Viewport3D.cpp
            Window3D.h Window3D.cpp
            SketchView.h SketchView.cpp
            SketchEditorDialog.h SketchEditorDialog.cpp
            SketchFeatureDialog.h SketchFeatureDialog.cpp
            AssemblyWindow.h AssemblyWindow.cpp
            Board3DWindow.h Board3DWindow.cpp)
        target_compile_definitions(kumcad PRIVATE LCAD_HAS_OCCT)
    endif()
endif()
