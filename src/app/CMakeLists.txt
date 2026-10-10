# framext：桌面应用壳（UI 层）。业务逻辑一律下沉 core/render，此处只做装配。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

qt_add_executable(framext WIN32
    main.cpp
    MainWindow.h MainWindow.cpp
    TitleBar.h TitleBar.cpp
    Theme.h
    controllers/ProjectController.h controllers/ProjectController.cpp
    controllers/ImportController.h controllers/ImportController.cpp
    controllers/ExportController.h controllers/ExportController.cpp
    panels/BasicInfoPanel.h panels/BasicInfoPanel.cpp
    panels/ExportPanel.h panels/ExportPanel.cpp
    panels/PhotoListPanel.h panels/PhotoListPanel.cpp
    panels/WatermarkPanel.h panels/WatermarkPanel.cpp
    canvas/PreviewCanvas.h canvas/PreviewCanvas.cpp
    ../../resources.qrc
)

target_link_libraries(framext PRIVATE
    framext_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# cpp 内以目录前缀 include 自己的头（controllers/... 等）
target_include_directories(framext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 窗口样式调整（user32）与系统圆角/阴影（dwmapi）
if(WIN32)
    target_link_libraries(framext PRIVATE user32 dwmapi)
endif()
