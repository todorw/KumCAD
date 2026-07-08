find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(kumcad
    main.cpp
    MainWindow.h
    MainWindow.cpp
    DrawingView.h
    DrawingView.cpp
    CommandLine.h
    CommandLine.cpp
    CommandDispatcher.h
    CommandDispatcher.cpp
    IconFactory.h
    IconFactory.cpp
    LayerPanel.h
    LayerPanel.cpp
    commands/DrawCommand.h
    commands/LineCommand.h
    commands/LineCommand.cpp
    commands/CircleCommand.h
    commands/CircleCommand.cpp
    commands/ArcCommand.h
    commands/ArcCommand.cpp
    commands/PolylineCommand.h
    commands/PolylineCommand.cpp
    commands/EllipseCommand.h
    commands/EllipseCommand.cpp
    commands/TextCommand.h
    commands/TextCommand.cpp
    commands/MoveCommand.h
    commands/MoveCommand.cpp
    commands/CopyCommand.h
    commands/CopyCommand.cpp
    commands/RotateCommand.h
    commands/RotateCommand.cpp
    commands/ScaleCommand.h
    commands/ScaleCommand.cpp
)

target_include_directories(kumcad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kumcad PRIVATE lcad_core Qt6::Widgets Qt6::OpenGLWidgets)
