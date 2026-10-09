# framext：桌面应用壳（UI 层）。业务逻辑一律下沉 core/render，此处只做装配。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(framext WIN32
    main.cpp
    MainWindow.h MainWindow.cpp
    TitleBar.h TitleBar.cpp
    ../../resources.qrc
)

target_link_libraries(framext PRIVATE
    framext_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# 窗口样式调整（user32）与系统圆角/阴影（dwmapi）
if(WIN32)
    target_link_libraries(framext PRIVATE user32 dwmapi)
endif()
