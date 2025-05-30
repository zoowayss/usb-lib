# 公共库
add_library(usb_common STATIC
    protocol/usbip_protocol.cpp
    protocol/usb_types.cpp
    network/tcp_socket.cpp
    network/message_handler.cpp
    utils/logger.cpp
    utils/buffer.cpp
)

target_include_directories(usb_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(usb_common
    Threads::Threads
)
