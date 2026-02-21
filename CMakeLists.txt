cmake_minimum_required(VERSION 3.16)
project(ABLTool VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
find_package(LibLZMA REQUIRED)

qt_add_executable(ABLTool
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/AblWorker.cpp
    src/AblWorker.h
    src/FvhParser.cpp
    src/FvhParser.h
    src/HexEditor.cpp
    src/HexEditor.h
)

target_link_libraries(ABLTool PRIVATE
    Qt6::Widgets
    Qt6::Core
    LibLZMA::LibLZMA
)

target_include_directories(ABLTool PRIVATE src)
