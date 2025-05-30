# 测试
add_executable(test_protocol
    test_protocol.cpp
)

target_link_libraries(test_protocol
    usb_common
    Threads::Threads
)

add_executable(test_network
    test_network.cpp
)

target_link_libraries(test_network
    usb_common
    Threads::Threads
)

# 添加测试
add_test(NAME protocol_test COMMAND test_protocol)
add_test(NAME network_test COMMAND test_network)
