cmake_minimum_required(VERSION 3.15)

# 展平后的目录结构：本目录同时是库（推理链）和 demo 主程序。
# 原上游 npu_flow/CMakeLists.txt 以 utils/*.cpp utils/media/*.cpp 编
# flow_utils 静态库，yolo_demo_hdmi_camera/ 子目录编可执行文件；展平后
# 合并到这里，显式分组，不能用 GLOB *.cpp（会把库源码误收进可执行文件）。

find_package(OpenCV REQUIRED)
function(target_link_opencv)
    foreach (arg ${ARGV})
        target_link_libraries(${arg} PRIVATE ${OpenCV_LIBS})
    endforeach ()
endfunction()

# --- 推理链静态库（对应上游 flow_utils） ---
file(GLOB FLOW_SRC *.cpp)
list(REMOVE_ITEM FLOW_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/solution.cpp)
# DRM 显示路径需要 libdrm 与 utils/drm 头，本次入库已删除 drm/，不编入，
# 显示走 hdmi_framebuffer（ENABLE_LIBDRM_HDMI 未定义时 hdmi_drm.cpp 为空翻译单元）
list(REMOVE_ITEM FLOW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/hdmi_drm.cpp)
add_library(flow_utils STATIC ${FLOW_SRC})
target_include_directories(flow_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(flow_utils PUBLIC ${OpenCV_LIBS})

# --- ArmNN（板上 SDK 安装路径，交叉编译时由外部传入 Armnn_DIR） ---
if(Armnn_DIR)
    target_include_directories(flow_utils PUBLIC ${Armnn_DIR}/include)
    target_link_libraries(flow_utils PUBLIC armnn armnnUtils armnnOnnxParser protobuf fmt)
    target_link_directories(flow_utils PUBLIC ${Armnn_DIR}/lib)
endif()

# --- demo 可执行文件（对应上游 yolo_demo_hdmi_camera） ---
add_executable(yolo_demo_hdmi_camera main.cpp solution.cpp)
target_link_libraries(yolo_demo_hdmi_camera PRIVATE flow_utils)
target_link_opencv(yolo_demo_hdmi_camera)
install(TARGETS yolo_demo_hdmi_camera)
