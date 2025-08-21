#include "gisstorage/gis_storage.h"
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

using namespace GisStorage;

class CompatibilityTest : public ::testing::Test {
  protected:
    std::string test_dir;
    std::string shapefile_path;
    std::unique_ptr<ShapefileConverter> converter;
    std::unique_ptr<GisStorageSystem> storage_system;
    std::vector<uint64_t> valid_fids;

    void SetUp() override {
        test_dir = "/home/chenming/Projects/test/s2-test/output_data/compatibility_test";
        shapefile_path = "/home/chenming/Projects/test/s2-test/data/test.shp";

        // 检查测试文件是否存在
        if (!std::filesystem::exists(shapefile_path)) {
            GTEST_SKIP() << "测试Shapefile不存在: " << shapefile_path;
        }

        // 创建输出目录
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // 清理测试产生的文件
        if (std::filesystem::exists(test_dir)) {
            std::filesystem::remove_all(test_dir);
        }
    }
};

// 测试Shapefile转换功能
TEST_F(CompatibilityTest, ShapefileConversion) {
    // 创建转换器
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);

    // 执行转换
    std::cout << "开始转换Shapefile..." << std::endl;
    valid_fids = converter->convert();

    EXPECT_FALSE(valid_fids.empty()) << "转换后应该有有效的要素";
    std::cout << "转换完成，有效要素数量: " << valid_fids.size() << std::endl;
}

// 测试生成的文件
TEST_F(CompatibilityTest, GeneratedFiles) {
    // 先执行转换
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    // 验证生成的文件
    std::string geom_file = converter->getGeometryFilePath();
    std::string attr_file = converter->getAttributeFilePath();
    std::string index_file = converter->getIndexFilePath();

    std::cout << "生成的文件:" << std::endl;
    std::cout << "- 几何文件: " << geom_file << std::endl;
    std::cout << "- 属性文件: " << attr_file << std::endl;
    std::cout << "- 索引文件: " << index_file << std::endl;

    // 检查文件是否存在
    EXPECT_TRUE(std::filesystem::exists(geom_file)) << "几何文件应该存在";
    EXPECT_TRUE(std::filesystem::exists(attr_file)) << "属性文件应该存在";
    EXPECT_TRUE(std::filesystem::exists(index_file)) << "索引文件应该存在";

    // 检查文件大小
    EXPECT_GT(std::filesystem::file_size(geom_file), 0) << "几何文件不应该为空";
    EXPECT_GT(std::filesystem::file_size(attr_file), 0) << "属性文件不应该为空";
    EXPECT_GT(std::filesystem::file_size(index_file), 0) << "索引文件不应该为空";
}

// 测试JSON索引文件格式
TEST_F(CompatibilityTest, JsonIndexFormat) {
    // 先执行转换
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    std::string index_file = converter->getIndexFilePath();
    ASSERT_TRUE(std::filesystem::exists(index_file));

    // 读取并解析索引文件
    std::ifstream index_stream(index_file);
    ASSERT_TRUE(index_stream.is_open()) << "应该能够打开索引文件";

    try {
        nlohmann::json index_data = nlohmann::json::parse(index_stream);

        // 验证JSON结构
        EXPECT_TRUE(index_data.contains("version")) << "索引文件应该包含version字段";
        EXPECT_TRUE(index_data.contains("data")) << "索引文件应该包含data字段";
        EXPECT_TRUE(index_data["data"].contains("features")) << "data应该包含features字段";

        EXPECT_EQ(index_data["version"], 1) << "版本应该是1";
        EXPECT_EQ(index_data["data"]["features"].size(), valid_fids.size()) << "要素数量应该匹配";

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
        FAIL() << "JSON解析失败: " << e.what();
    }
}

// 测试存储系统初始化
TEST_F(CompatibilityTest, StorageSystemInitialization) {
    // 先执行转换
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    // 创建存储系统并初始化
    storage_system = std::make_unique<GisStorageSystem>(test_dir);

    EXPECT_NO_THROW({ storage_system->initializeStorageFiles(shapefile_path); }) << "存储系统初始化不应该抛出异常";
}

// 测试几何数据读取
TEST_F(CompatibilityTest, GeometryDataReading) {
    // 先执行转换和初始化
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    storage_system = std::make_unique<GisStorageSystem>(test_dir);
    storage_system->initializeStorageFiles(shapefile_path);

    // 测试读取第一个要素
    uint64_t test_fid = valid_fids[0];
    std::cout << "读取要素 ID: " << test_fid << std::endl;

    std::shared_ptr<GeometryData> geom;
    EXPECT_NO_THROW({ geom = storage_system->readGeometry(test_fid); }) << "几何数据读取不应该抛出异常";

    ASSERT_TRUE(geom) << "应该能够读取几何数据";

    std::cout << "✓ 几何数据读取成功" << std::endl;
    std::cout << "  几何类型: " << static_cast<int>(geom->getGeometryType()) << std::endl;
    std::cout << "  边界框: (" << geom->getBBox().min_x << ", " << geom->getBBox().min_y << ") - (" << geom->getBBox().max_x << ", " << geom->getBBox().max_y << ")" << std::endl;

    auto coords = geom->decodeCoordinates();
    EXPECT_GT(coords.size(), 0) << "坐标点数量应该大于0";
    std::cout << "  坐标点数量: " << coords.size() << std::endl;
}

// 测试属性数据读取
TEST_F(CompatibilityTest, AttributeDataReading) {
    // 先执行转换和初始化
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    storage_system = std::make_unique<GisStorageSystem>(test_dir);
    storage_system->initializeStorageFiles(shapefile_path);

    // 测试读取第一个要素
    uint64_t test_fid = valid_fids[0];
    std::cout << "读取要素 ID: " << test_fid << std::endl;

    std::shared_ptr<AttributeData> attr;
    EXPECT_NO_THROW({ attr = storage_system->readAttribute(test_fid); }) << "属性数据读取不应该抛出异常";

    ASSERT_TRUE(attr) << "应该能够读取属性数据";

    std::cout << "✓ 属性数据读取成功" << std::endl;
    std::cout << "  属性数量: " << attr->getProperties().size() << std::endl;

    EXPECT_GE(attr->getProperties().size(), 0) << "属性数量应该大于等于0";
}

// 测试批量读取功能
TEST_F(CompatibilityTest, BatchReading) {
    // 先执行转换和初始化
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    storage_system = std::make_unique<GisStorageSystem>(test_dir);
    storage_system->initializeStorageFiles(shapefile_path);

    // 测试批量读取前5个要素
    int test_count = std::min(5, static_cast<int>(valid_fids.size()));
    int success_count = 0;

    for (int i = 0; i < test_count; ++i) {
        uint64_t fid = valid_fids[i];
        try {
            auto geom = storage_system->readGeometry(fid);
            auto attr = storage_system->readAttribute(fid);

            if (geom && attr) {
                success_count++;
                EXPECT_GT(geom->decodeCoordinates().size(), 0) << "要素 " << fid << " 应该有坐标";
                EXPECT_GE(attr->getProperties().size(), 0) << "要素 " << fid << " 应该有属性";
            }
        } catch (const std::exception& e) {
            std::cout << "读取要素 " << fid << " 失败: " << e.what() << std::endl;
        }
    }

    EXPECT_GT(success_count, 0) << "至少应该成功读取一个要素";
    std::cout << "批量读取成功: " << success_count << "/" << test_count << " 个要素" << std::endl;
}

// 测试错误处理
TEST_F(CompatibilityTest, ErrorHandling) {
    // 先执行转换和初始化
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    storage_system = std::make_unique<GisStorageSystem>(test_dir);
    storage_system->initializeStorageFiles(shapefile_path);

    // 测试读取不存在的FID
    uint64_t non_existent_fid = 999999;

    try {
        auto geom = storage_system->readGeometry(non_existent_fid);
        auto attr = storage_system->readAttribute(non_existent_fid);

        // 如果返回nullptr，这是可以接受的
        if (!geom && !attr) {
            std::cout << "读取不存在的FID返回nullptr（正常行为）" << std::endl;
        } else {
            FAIL() << "读取不存在的FID应该返回nullptr";
        }
    } catch (const std::exception& e) {
        // 如果抛出异常，这也是可以接受的错误处理方式
        std::cout << "读取不存在的FID时抛出异常: " << e.what() << std::endl;
    }
}

// 测试文件完整性
TEST_F(CompatibilityTest, FileIntegrity) {
    // 先执行转换
    converter = std::make_unique<ShapefileConverter>(shapefile_path, test_dir);
    valid_fids = converter->convert();
    ASSERT_FALSE(valid_fids.empty());

    std::string geom_file = converter->getGeometryFilePath();
    std::string attr_file = converter->getAttributeFilePath();
    std::string index_file = converter->getIndexFilePath();

    // 检查文件权限
    EXPECT_TRUE(std::filesystem::is_regular_file(geom_file)) << "几何文件应该是普通文件";
    EXPECT_TRUE(std::filesystem::is_regular_file(attr_file)) << "属性文件应该是普通文件";
    EXPECT_TRUE(std::filesystem::is_regular_file(index_file)) << "索引文件应该是普通文件";

    // 检查文件是否可读
    std::ifstream geom_stream(geom_file);
    std::ifstream attr_stream(attr_file);
    std::ifstream index_stream(index_file);

    EXPECT_TRUE(geom_stream.good()) << "几何文件应该可读";
    EXPECT_TRUE(attr_stream.good()) << "属性文件应该可读";
    EXPECT_TRUE(index_stream.good()) << "索引文件应该可读";
}
