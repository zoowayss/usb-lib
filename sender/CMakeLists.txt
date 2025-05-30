# 发送端 (macOS)
add_executable(usb_sender
    main.cpp
    usb/usb_device_manager.cpp
    usb/mass_storage_device.cpp
    capture/urb_capture.cpp
)

target_include_directories(usb_sender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(usb_sender
    usb_common
    PkgConfig::LIBUSB
    ${IOKIT_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
    Threads::Threads
)

target_compile_definitions(usb_sender PRIVATE
    ${LIBUSB_CFLAGS_OTHER}
)
