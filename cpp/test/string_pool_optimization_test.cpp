//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/optimized_shapefile_converter.h"
#include "gisstorage/optimized_attribute_storage.h"
#include "gisstorage/string_pool.h"
#include "gisstorage/shapefile_converter.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace GisStorage;

class StringPoolOptimizationTest : public ::testing::Test {
  protected:
    std::string test_shapefile;
    std::string output_dir;
    std::string optimized_output_dir;
    std::vector<uint64_t> valid_fids;
    std::unique_ptr<ShapefileConverter> original_converter;
    std::unique_ptr<OptimizedShapefileConverter> optimized_converter;

    void SetUp() override {
        // 设置测试文件路径
        test_shapefile = "/home/chenming/Data/GIS_DATA/shapefile/dltb_532300_2020.shp";
        output_dir = "./test_output/original";
        optimized_output_dir = "./test_output/optimized";

        // 检查测试文件是否存在
        if (!std::filesystem::exists(test_shapefile)) {
            GTEST_SKIP() << "测试Shapefile不存在: " << test_shapefile;
        }

        // 清理旧的输出文件
        if (std::filesystem::exists(output_dir)) {
            std::filesystem::remove_all(output_dir);
        }
        if (std::filesystem::exists(optimized_output_dir)) {
            std::filesystem::remove_all(optimized_output_dir);
        }

        // 创建测试输出目录
        std::filesystem::create_directories(output_dir);
        std::filesystem::create_directories(optimized_output_dir);

        // 重置转换器
        original_converter.reset();
        optimized_converter.reset();
    }

    void TearDown() override {
        // 清理测试产生的文件
        // if (std::filesystem::exists(output_dir)) {
        //     std::filesystem::remove_all(output_dir);
        // }
        // if (std::filesystem::exists(optimized_output_dir)) {
        //     std::filesystem::remove_all(optimized_output_dir);
        // }
    }
};

// 字符串池基本功能测试
TEST_F(StringPoolOptimizationTest, StringPoolBasicFunctionality) {
    StringPool pool;

    // 测试添加字符串
    uint32_t id1 = pool.getStringId("test");
    uint32_t id2 = pool.getStringId("test");
    uint32_t id3 = pool.getStringId("another");

    EXPECT_EQ(id1, id2); // 相同字符串应该返回相同ID
    EXPECT_NE(id1, id3); // 不同字符串应该返回不同ID

    // 测试获取字符串
    EXPECT_EQ(pool.getString(id1), "test");
    EXPECT_EQ(pool.getString(id3), "another");
    EXPECT_EQ(pool.getString(999), ""); // 不存在的ID应该返回空字符串

    // 测试统计信息
    EXPECT_EQ(pool.getPoolSize(), 2); // 应该有2个唯一字符串
    EXPECT_GT(pool.getTotalSize(), 0);
}

// 字符串池序列化测试
TEST_F(StringPoolOptimizationTest, StringPoolSerialization) {
    StringPool pool;

    // 添加一些测试字符串
    pool.getStringId("field1");
    pool.getStringId("field2");
    pool.getStringId("value1");
    pool.getStringId("value2");
    pool.getStringId("value1"); // 重复字符串

    // 序列化
    auto serialized = pool.serialize();
    EXPECT_GT(serialized.size(), 0);

    // 反序列化到新的池
    StringPool new_pool;
    new_pool.deserialize(serialized);

    // 验证数据完整性
    EXPECT_EQ(new_pool.getPoolSize(), pool.getPoolSize());
    EXPECT_EQ(new_pool.getString(0), "field1");
    EXPECT_EQ(new_pool.getString(1), "field2");
    EXPECT_EQ(new_pool.getString(2), "value1");
    EXPECT_EQ(new_pool.getString(3), "value2");
}

// 优化的属性序列化器测试
TEST_F(StringPoolOptimizationTest, OptimizedAttributeSerializer) {
    OptimizedAttributeSerializer serializer;

    // 创建测试属性数据
    std::map<std::string, std::string> properties = {
      {"name", "测试要素"},
      {"type", "道路"},
      {"length", "100.5"},
      {"width", "5.0"},
      {"name", "测试要素"}, // 重复值
      {"type", "道路"}      // 重复值
    };

    AttributeData attr(12345, properties);

    // 序列化
    auto serialized = serializer.serializeAttributes(attr);
    EXPECT_GT(serialized.size(), 0);

    // 反序列化
    auto deserialized = serializer.deserializeAttributes(serialized);
    EXPECT_EQ(deserialized->getFeatureId(), 12345);
    EXPECT_EQ(deserialized->getProperties().size(), 4); // 去重后应该是4个属性

    // 验证压缩统计
    auto stats = serializer.getCompressionStats();
    EXPECT_GT(stats.unique_strings, 0);
    EXPECT_GT(stats.compression_ratio, 0.0);

    std::cout << "字符串池统计:" << std::endl;
    std::cout << "  唯一字符串数: " << stats.unique_strings << std::endl;
    std::cout << "  压缩率: " << stats.compression_ratio << "%" << std::endl;
}

// 原始vs优化转换对比测试
TEST_F(StringPoolOptimizationTest, OriginalVsOptimizedConversion) {
    // 执行原始转换
    auto start_time = std::chrono::high_resolution_clock::now();
    original_converter = std::make_unique<ShapefileConverter>(test_shapefile, output_dir);
    auto original_fids = original_converter->convert();
    auto original_time = std::chrono::high_resolution_clock::now();
    auto original_duration = std::chrono::duration_cast<std::chrono::milliseconds>(original_time - start_time);

    // 验证原始转换成功
    EXPECT_GT(original_fids.size(), 0) << "原始转换应该成功";
    EXPECT_TRUE(std::filesystem::exists(original_converter->getAttributeFilePath())) << "原始属性文件应该存在";
    EXPECT_TRUE(std::filesystem::exists(original_converter->getGeometryFilePath())) << "原始几何文件应该存在";

    // 执行优化转换
    start_time = std::chrono::high_resolution_clock::now();
    optimized_converter = std::make_unique<OptimizedShapefileConverter>(test_shapefile, optimized_output_dir);
    auto optimized_fids = optimized_converter->convert();
    auto optimized_time = std::chrono::high_resolution_clock::now();
    auto optimized_duration = std::chrono::duration_cast<std::chrono::milliseconds>(optimized_time - start_time);

    // 验证优化转换成功
    EXPECT_GT(optimized_fids.size(), 0) << "优化转换应该成功";
    EXPECT_TRUE(std::filesystem::exists(optimized_converter->getAttributeFilePath())) << "优化属性文件应该存在";
    EXPECT_TRUE(std::filesystem::exists(optimized_converter->getStringPoolFilePath())) << "字符串池文件应该存在";

    // 验证要素数量
    EXPECT_EQ(original_fids.size(), optimized_fids.size());
    valid_fids = optimized_fids;

    // 获取文件大小
    auto original_attr_size = std::filesystem::file_size(original_converter->getAttributeFilePath());
    auto optimized_attr_size = std::filesystem::file_size(optimized_converter->getAttributeFilePath());
    auto string_pool_size = std::filesystem::file_size(optimized_converter->getStringPoolFilePath());

    // 计算压缩效果
    double compression_ratio = (1.0 - (double)(optimized_attr_size + string_pool_size) / original_attr_size) * 100.0;

    // 获取优化转换的统计信息
    auto conversion_stats = optimized_converter->getConversionStats();
    auto compression_stats = optimized_converter->getCompressionStats();

    // 输出对比结果
    std::cout << "\n=== 原始vs优化转换对比 ===" << std::endl;
    std::cout << "要素数量: " << valid_fids.size() << std::endl;
    std::cout << "原始转换时间: " << original_duration.count() << "ms" << std::endl;
    std::cout << "优化转换时间: " << optimized_duration.count() << "ms" << std::endl;
    std::cout << "时间提升: " << (original_duration.count() - optimized_duration.count()) * 100.0 / original_duration.count() << "%" << std::endl;

    std::cout << "\n=== 存储空间对比 ===" << std::endl;
    std::cout << "原始属性文件大小: " << original_attr_size << " 字节" << std::endl;
    std::cout << "优化属性文件大小: " << optimized_attr_size << " 字节" << std::endl;
    std::cout << "字符串池文件大小: " << string_pool_size << " 字节" << std::endl;
    std::cout << "总压缩率: " << compression_ratio << "%" << std::endl;

    std::cout << "\n=== 字符串池统计 ===" << std::endl;
    std::cout << "唯一字符串数: " << compression_stats.unique_strings << std::endl;
    std::cout << "总字符串数: " << compression_stats.total_strings << std::endl;
    std::cout << "压缩率: " << compression_stats.compression_ratio << "%" << std::endl;
    std::cout << "节省空间: " << compression_stats.original_size - compression_stats.compressed_size << " 字节" << std::endl;

    // 验证压缩效果
    EXPECT_GT(compression_ratio, 0.0) << "应该有压缩效果";
    EXPECT_GT(compression_stats.compression_ratio, 0.0) << "应该有压缩效果";
    EXPECT_GT(compression_stats.unique_strings, 0) << "应该有唯一字符串";
}

// 优化存储读取测试
TEST_F(StringPoolOptimizationTest, OptimizedStorageReadTest) {
    // 如果还没有执行优化转换，先执行一次
    if (!optimized_converter) {
        optimized_converter = std::make_unique<OptimizedShapefileConverter>(test_shapefile, optimized_output_dir);
        auto optimized_fids = optimized_converter->convert();
        valid_fids = optimized_fids;
    }

    // 创建优化的属性存储
    auto attr_storage = std::make_unique<OptimizedAttributeStorage>(optimized_converter->getAttributeFilePath(), optimized_converter->getStringPoolFilePath());

    // 加载索引
    attr_storage->loadIndexFromFile(optimized_converter->getIndexFilePath());

    // 测试读取一些要素
    auto test_fids = std::vector<uint64_t>(valid_fids.begin(), valid_fids.begin() + std::min(valid_fids.size(), size_t(10)));

    int success_count = 0;
    for (uint64_t fid : test_fids) {
        try {
            auto attr = attr_storage->readAttribute(fid);
            if (attr) {
                success_count++;
                EXPECT_EQ(attr->getFeatureId(), fid);
                EXPECT_GT(attr->getProperties().size(), 0);
            }
        } catch (const std::exception& e) {
            std::cout << "读取FID " << fid << " 时发生异常: " << e.what() << std::endl;
        }
    }

    EXPECT_GT(success_count, 0) << "应该能成功读取一些要素";

    // 获取存储统计信息
    auto storage_stats = attr_storage->getStorageStats();
    auto compression_stats = attr_storage->getCompressionStats();

    std::cout << "\n=== 优化存储读取测试 ===" << std::endl;
    std::cout << "成功读取要素数: " << success_count << "/" << test_fids.size() << std::endl;
    std::cout << "总要素数: " << storage_stats.total_features << std::endl;
    std::cout << "压缩率: " << storage_stats.compression_ratio << "%" << std::endl;
    std::cout << "字符串池大小: " << storage_stats.string_pool_size << std::endl;
    std::cout << "节省空间: " << storage_stats.string_pool_saved_bytes << " 字节" << std::endl;
}

// 性能对比测试
TEST_F(StringPoolOptimizationTest, PerformanceComparison) {
    // 如果还没有执行优化转换，先执行一次
    if (!optimized_converter) {
        optimized_converter = std::make_unique<OptimizedShapefileConverter>(test_shapefile, optimized_output_dir);
        auto optimized_fids = optimized_converter->convert();
        valid_fids = optimized_fids;
    }

    // 创建优化的属性存储
    auto attr_storage = std::make_unique<OptimizedAttributeStorage>(optimized_converter->getAttributeFilePath(), optimized_converter->getStringPoolFilePath());

    // 加载索引
    attr_storage->loadIndexFromFile(optimized_converter->getIndexFilePath());

    // 性能测试 - 读取所有要素
    auto start_time = std::chrono::high_resolution_clock::now();

    int success_count = 0;
    for (uint64_t fid : valid_fids) {
        try {
            auto attr = attr_storage->readAttribute(fid);
            if (attr) {
                success_count++;
            }
        } catch (const std::exception& e) {
            // 忽略读取失败的情况
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    double read_rate = success_count * 1000.0 / duration.count();

    std::cout << "\n=== 性能测试结果 ===" << std::endl;
    std::cout << "读取要素数: " << success_count << "/" << valid_fids.size() << std::endl;
    std::cout << "读取时间: " << duration.count() << "ms" << std::endl;
    std::cout << "读取速度: " << read_rate << " 要素/秒" << std::endl;

    // 验证性能要求
    EXPECT_GT(success_count, 0);
    EXPECT_GE(read_rate, 1000.0) << "读取速度应该至少1000要素/秒";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
