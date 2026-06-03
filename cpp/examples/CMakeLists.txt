# GIS存储系统演示程序CMakeLists.txt

# 创建演示程序
add_executable(gis_storage_demo 
    gis_storage_demo.cpp
)

# 设置目标属性
target_include_directories(gis_storage_demo PRIVATE 
    ../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# 链接库
target_link_libraries(gis_storage_demo PRIVATE
    gisstorage
)

# 设置C++标准
target_compile_features(gis_storage_demo PRIVATE cxx_std_17)

# 编译选项
target_compile_options(gis_storage_demo PRIVATE
    -Wall
    -Wextra
    -O2
)

# 安装规则
install(TARGETS gis_storage_demo
    RUNTIME DESTINATION bin
)

# 打印信息
message(STATUS "GIS存储系统演示程序配置完成")
message(STATUS "演示程序: gis_storage_demo")
message(STATUS "用法: gis_storage_demo <输入Shapefile> <输出目录>")
