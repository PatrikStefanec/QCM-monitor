cmake_minimum_required(VERSION 3.20)

project(TM13Monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
)

set(IMPLOT_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/external/implot
)

add_executable(TM13Monitor
    src/main.cpp
    src/SerialPort.h
    src/SerialPort.cpp
    src/PrevacProtocol.h
    src/PrevacProtocol.cpp
    src/SerialWorker.h
    src/MonitorChannel.h
    src/MonitorChannel.cpp
    src/ComPortList.h
    src/ComPortList.cpp
    
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp

    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp

    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

target_include_directories(TM13Monitor PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMPLOT_DIR}
)

target_link_libraries(TM13Monitor PRIVATE
    d3d11
    dxgi
    d3dcompiler
)