#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../include/gis_storage.h"

using namespace GisStorage;

int main() {
    std::cout << "=== C++ GIS Storage 兼容性测试 ===" << std::endl;

    // 测试文件路径
    std::string test_dir = "/home/chenming/Projects/test/s2-test/output_data/compatibility_test";
    std::string shapefile_path = "/home/chenming/Projects/test/s2-test/data/test.shp"; // 假设有这个测试文件

    try {
        // 创建转换器
        ShapefileConverter converter(shapefile_path, test_dir);

        // 执行转换
        std::cout << "开始转换Shapefile..." << std::endl;
        std::vector<uint64_t> valid_fids = converter.convert();

        std::cout << "转换完成，有效要素数量: " << valid_fids.size() << std::endl;

        // 验证生成的文件
        std::string geom_file = converter.getGeometryFilePath();
        std::string attr_file = converter.getAttributeFilePath();
        std::string index_file = converter.getIndexFilePath();

        std::cout << "生成的文件:" << std::endl;
        std::cout << "- 几何文件: " << geom_file << std::endl;
        std::cout << "- 属性文件: " << attr_file << std::endl;
        std::cout << "- 索引文件: " << index_file << std::endl;

        // 检查文件是否存在
        if (std::filesystem::exists(geom_file)) {
            std::cout << "✓ 几何文件创建成功" << std::endl;
        } else {
            std::cout << "✗ 几何文件创建失败" << std::endl;
        }

        if (std::filesystem::exists(attr_file)) {
            std::cout << "✓ 属性文件创建成功" << std::endl;
        } else {
            std::cout << "✗ 属性文件创建失败" << std::endl;
        }

        if (std::filesystem::exists(index_file)) {
            std::cout << "✓ 索引文件创建成功" << std::endl;

            // 读取并解析索引文件
            std::ifstream index_stream(index_file);
            if (index_stream.is_open()) {
                try {
                    nlohmann::json index_data = nlohmann::json::parse(index_stream);
                    std::cout << "索引文件格式: JSON" << std::endl;
                    std::cout << "版本: " << index_data["version"] << std::endl;
                    std::cout << "要素数量: " << index_data["data"]["features"].size() << std::endl;

                    // 显示前几个要素的索引信息
                    int count = 0;
                    for (const auto& feature : index_data["data"]["features"].items()) {
                        if (count < 3) {
                            std::cout << "  要素 " << feature.key() << ": geom_offset=" << feature.value()["geom_offset"] << ", attr_offset=" << feature.value()["attr_offset"] << std::endl;
                            count++;
                        } else {
                            break;
                        }
                    }
                } catch (const nlohmann::json::exception& e) {
                    std::cout << "JSON解析失败: " << e.what() << std::endl;
                }
            }
        } else {
            std::cout << "✗ 索引文件创建失败" << std::endl;
        }

        // 测试读取功能
        if (!valid_fids.empty()) {
            std::cout << "\n测试读取功能..." << std::endl;

            // 创建存储系统并初始化
            GisStorageSystem storage_system(test_dir);
            storage_system.initializeStorageFiles(shapefile_path);

            // 测试读取第一个要素
            uint64_t test_fid = valid_fids[0];
            std::cout << "读取要素 ID: " << test_fid << std::endl;

            try {
                auto geom = storage_system.readGeometry(test_fid);
                std::cout << "✓ 几何数据读取成功" << std::endl;
                std::cout << "  几何类型: " << static_cast<int>(geom->getGeometryType()) << std::endl;
                std::cout << "  边界框: (" << geom->getBBox().min_x << ", " << geom->getBBox().min_y << ") - (" << geom->getBBox().max_x << ", " << geom->getBBox().max_y << ")" << std::endl;

                auto coords = geom->decodeCoordinates();
                std::cout << "  坐标点数量: " << coords.size() << std::endl;

            } catch (const std::exception& e) {
                std::cout << "✗ 几何数据读取失败: " << e.what() << std::endl;
            }

            try {
                auto attr = storage_system.readAttribute(test_fid);
                if (attr) {
                    std::cout << "✓ 属性数据读取成功" << std::endl;
                    std::cout << "  属性数量: " << attr->getProperties().size() << std::endl;
                } else {
                    std::cout << "✗ 属性数据读取失败" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "✗ 属性数据读取失败: " << e.what() << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== 测试完成 ===" << std::endl;
    return 0;
}
