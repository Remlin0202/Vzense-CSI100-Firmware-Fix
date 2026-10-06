cmake_minimum_required(VERSION 3.8)
project(csi100_driver)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDCAM_710")

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# ---- Vzense SDK ------------------------------------------------------------
# Point CSI100_SDK_ROOT at the SDK, or drop/symlink it as ./sdk inside this
# package (that is what deploy_q6a.sh does).
set(SDK_ROOT "$ENV{CSI100_SDK_ROOT}")
if(NOT SDK_ROOT)
  set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  set(SDK_LIB_SUBDIR "aarch64")
else()
  set(SDK_LIB_SUBDIR "x86_64")
endif()
set(SDK_LIB_DIR "${SDK_ROOT}/Lib/${SDK_LIB_SUBDIR}")

if(NOT EXISTS "${SDK_ROOT}/Include/Vzense_api2.h")
  message(FATAL_ERROR "Vzense SDK not found at ${SDK_ROOT} "
                      "(set CSI100_SDK_ROOT or symlink ${CMAKE_CURRENT_SOURCE_DIR}/sdk)")
endif()
if(NOT EXISTS "${SDK_LIB_DIR}")
  message(FATAL_ERROR "no SDK libs for ${CMAKE_SYSTEM_PROCESSOR} in ${SDK_ROOT}/Lib")
endif()

message(STATUS "CSI100 SDK: ${SDK_ROOT} (${SDK_LIB_SUBDIR})")

add_executable(csi100_driver_node src/csi100_driver_node.cpp)
target_include_directories(csi100_driver_node PRIVATE "${SDK_ROOT}/Include")
target_link_directories(csi100_driver_node PRIVATE "${SDK_LIB_DIR}")
target_link_libraries(csi100_driver_node
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
  vzense_api
  pthread
  m)
# so the node finds libvzense_api.so at runtime without LD_LIBRARY_PATH
set_target_properties(csi100_driver_node PROPERTIES
  BUILD_RPATH "${SDK_LIB_DIR}"
  INSTALL_RPATH "${SDK_LIB_DIR}")

install(TARGETS csi100_driver_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
  OPTIONAL)

ament_package()
