cmake_minimum_required(VERSION 3.16)
project(so_drive VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_add_executable(so_drive
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    filewindow.cpp
    filewindow.h
    filewindow.ui
    dialognewitem.cpp
    dialognewitem.h
    dialognewitem.ui
    resources.qrc
)

target_link_libraries(so_drive PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(so_drive PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
