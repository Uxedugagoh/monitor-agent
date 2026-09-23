cmake_minimum_required(VERSION 3.20)

project(monitor_agent
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(activity_packet src/serialization/activity_packet.cpp)
target_include_directories(activity_packet PUBLIC include PRIVATE third_party)

add_library(backup_store src/storage/backup_store.cpp)
target_include_directories(backup_store PUBLIC include PRIVATE third_party)

add_library(shutdown_monitor src/runtime/shutdown_monitor.cpp)
target_include_directories(shutdown_monitor PUBLIC include)
target_link_libraries(shutdown_monitor PRIVATE User32)

add_library(input_activity src/collector/input_activity.cpp)
target_include_directories(input_activity PUBLIC include)
target_link_libraries(input_activity PRIVATE User32)

add_library(http_sender src/network/http_sender.cpp)
target_include_directories(http_sender PUBLIC include PRIVATE third_party/cpp-httplib)
target_link_libraries(http_sender PUBLIC activity_packet PRIVATE Ws2_32 Crypt32)

add_executable(demo_server tools/demo_server.cpp)
target_include_directories(demo_server PRIVATE third_party third_party/cpp-httplib)
target_link_libraries(demo_server PRIVATE Ws2_32 Crypt32)

include(CTest)
if(BUILD_TESTING)
    add_executable(activity_queue_test tests/activity_queue_test.cpp)
    target_include_directories(activity_queue_test PRIVATE include)
    add_test(NAME activity_queue COMMAND activity_queue_test)
    set_tests_properties(activity_queue PROPERTIES TIMEOUT 15)
    add_executable(activity_packet_test tests/activity_packet_test.cpp)
    target_include_directories(activity_packet_test PRIVATE third_party)
    target_link_libraries(activity_packet_test PRIVATE activity_packet)
    add_test(NAME activity_packet COMMAND activity_packet_test)
    set_tests_properties(activity_packet PROPERTIES TIMEOUT 15)
    add_executable(http_sender_test tests/http_sender_test.cpp)
    target_include_directories(http_sender_test PRIVATE third_party third_party/cpp-httplib)
    target_link_libraries(http_sender_test PRIVATE http_sender Ws2_32 Crypt32)
    add_test(NAME http_sender COMMAND http_sender_test)
    set_tests_properties(http_sender PROPERTIES TIMEOUT 30)
    add_executable(backup_store_test tests/backup_store_test.cpp)
    target_link_libraries(backup_store_test PRIVATE backup_store)
    add_test(NAME backup_store COMMAND backup_store_test)
    set_tests_properties(backup_store PROPERTIES TIMEOUT 15)
    add_executable(backup_lifecycle_test tests/backup_lifecycle_test.cpp src/collector/activity_collector.cpp)
    target_link_libraries(backup_lifecycle_test PRIVATE backup_store http_sender shutdown_monitor input_activity Psapi User32)
    add_test(NAME backup_lifecycle COMMAND backup_lifecycle_test)
    set_tests_properties(backup_lifecycle PROPERTIES TIMEOUT 15)
    add_executable(shutdown_monitor_test tests/shutdown_monitor_test.cpp)
    target_link_libraries(shutdown_monitor_test PRIVATE shutdown_monitor backup_store User32)
    add_test(NAME shutdown_monitor COMMAND shutdown_monitor_test)
    set_tests_properties(shutdown_monitor PROPERTIES TIMEOUT 15)
    add_executable(input_activity_test tests/input_activity_test.cpp)
    target_link_libraries(input_activity_test PRIVATE input_activity)
    add_test(NAME input_activity COMMAND input_activity_test)
    set_tests_properties(input_activity PROPERTIES TIMEOUT 15)
    add_executable(recovery_delivery_test tests/recovery_delivery_test.cpp)
    target_include_directories(recovery_delivery_test PRIVATE third_party third_party/cpp-httplib)
    target_link_libraries(recovery_delivery_test PRIVATE backup_store http_sender Ws2_32 Crypt32)
    add_test(NAME recovery_delivery COMMAND recovery_delivery_test)
    set_tests_properties(recovery_delivery PROPERTIES TIMEOUT 20)
endif()

add_executable(monitor_agent
    src/main.cpp
    src/collector/activity_collector.cpp
)

target_include_directories(monitor_agent
    PRIVATE
        include
)

target_link_libraries(monitor_agent
    PRIVATE
        Psapi
        User32
        activity_packet
        http_sender
        backup_store
        shutdown_monitor
        input_activity
)
