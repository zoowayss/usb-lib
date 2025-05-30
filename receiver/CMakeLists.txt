# 接收端 (Linux)
add_executable(usb_receiver
    main.cpp
    usbip/usbip_client.cpp
    virtual_device/virtual_usb_device.cpp
)

target_include_directories(usb_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(usb_receiver
    usb_common
    Threads::Threads
)

# Linux特定的编译选项
if(UNIX AND NOT APPLE)
    target_compile_definitions(usb_receiver PRIVATE
        _GNU_SOURCE
        __LINUX__
    )

    # 链接Linux系统库
    target_link_libraries(usb_receiver
        rt  # 实时扩展库
        dl  # 动态链接库
    )
endif()
