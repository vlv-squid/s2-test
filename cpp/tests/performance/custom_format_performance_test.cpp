#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <iomanip>
#include <sstream>
#include <memory>

// 自定义GIS存储格式头文件
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/attribute_data.h"
#include "gisstorage/types.h"
#include "gisindex/s2spatial_index.h"

class CustomFormatPerformanceTest {
  private:
    std::string dataset_name_;
    std::string data_dir_;
    std::unique_ptr<GisStorage::GisStorageSystem> storage_system_;
    size_t total_features_;
    GisStorage::BBox data_extent_;

  public:
    CustomFormatPerformanceTest(const std::string& data_dir, const std::string& dataset_name)
        : dataset_name_(dataset_name)
        , data_dir_(data_dir)
        , storage_system_(nullptr)
        , total_features_(0) {
        // 初始化存储系统
        storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(data_dir_);

        // 设置数据集名称（完整加载，包括几何和属性存储）
        storage_system_->SetDatasetName(dataset_name_);

        // 加载元数据获取基本信息
        storage_system_->LoadMetadata();
        const auto& metadata = storage_system_->GetMetadata();

        total_features_ = metadata.total_features;
        data_extent_ = metadata.spatial_extent;

        // 如果空间范围无效（所有值都是0），尝试从S2索引获取
        if ((data_extent_.min_x == 0.0 && data_extent_.min_y == 0.0 && data_extent_.max_x == 0.0 && data_extent_.max_y == 0.0) && storage_system_->IsS2IndexValid()) {
            // 使用一个大的查询范围来获取数据的实际范围
            // 这里我们使用一个合理的默认范围，实际应用中应该从S2索引中获取
            data_extent_ = GisStorage::BBox(97.0, 21.0, 106.0, 30.0); // 云南省的大致范围
        }
    }

    ~CustomFormatPerformanceTest() { storage_system_.reset(); }

    // 检查自定义格式是否有效
    bool isValid() const { return storage_system_ != nullptr && total_features_ > 0; }

    // 获取总要素数
    size_t getTotalFeatures() const { return total_features_; }

    // 获取数据范围
    const GisStorage::BBox& getDataExtent() const { return data_extent_; }

    // 测试顺序读取所有要素（轻量级模式：只测试S2索引查询）
    double testS2IndexSequentialReadAll() {
        if (!isValid())
            return -1.0;

        auto start_time = std::chrono::high_resolution_clock::now();

        // 在轻量级模式下，我们只测试S2索引的空间查询性能
        // 使用整个数据范围进行查询来模拟"读取所有要素"
        const auto& extent = data_extent_;
        auto result_ids = storage_system_->QueryS2Index(extent);
        size_t count = result_ids.size();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试随机读取指定数量的要素（轻量级模式：随机空间查询）
    std::pair<double, size_t> testS2IndexRandomReadFeatures(int num_reads = 1000) {
        if (!isValid() || total_features_ == 0)
            return {-1.0, 0};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> x_dis(data_extent_.min_x, data_extent_.max_x);
        std::uniform_real_distribution<double> y_dis(data_extent_.min_y, data_extent_.max_y);
        std::uniform_real_distribution<double> size_dis(0.001, 0.01); // 很小的查询范围

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t total_features_found = 0;

        for (int i = 0; i < num_reads; ++i) {
            // 生成随机小范围查询
            double center_x = x_dis(gen);
            double center_y = y_dis(gen);
            double range = size_dis(gen);

            GisStorage::BBox query_bbox(center_x - range, center_y - range, center_x + range, center_y + range);

            auto result_ids = storage_system_->QueryS2Index(query_bbox);
            total_features_found += result_ids.size();
            // 在轻量级模式下，我们只测试S2索引查询，不实际读取几何数据
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, total_features_found}; // 返回毫秒和找到的要素总数
    }

    // 测试基于bbox的顺序读取所有要素
    double testSequentialReadAll() {
        if (!isValid())
            return -1.0;

        auto start_time = std::chrono::high_resolution_clock::now();

        // 直接获取所有要素ID，然后批量读取几何数据
        auto all_feature_ids = storage_system_->GetAllFeatureIds();

        if (all_feature_ids.empty()) {
            return 0.0;
        }

        // 限制读取数量以避免测试时间过长（对于大数据集）
        size_t max_features = std::min(all_feature_ids.size(), size_t(10000));
        std::vector<uint64_t> limited_ids(all_feature_ids.begin(), all_feature_ids.begin() + max_features);

        auto geometries = storage_system_->ReadGeometries(limited_ids);
        size_t count = geometries.size();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试基于bbox的随机读取指定数量的要素
    std::pair<double, size_t> testBBoxRandomReadFeatures(int num_reads = 1000) {
        if (!isValid() || total_features_ == 0)
            return {-1.0, 0};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> x_dis(data_extent_.min_x, data_extent_.max_x);
        std::uniform_real_distribution<double> y_dis(data_extent_.min_y, data_extent_.max_y);
        std::uniform_real_distribution<double> size_dis(0.001, 0.01); // 很小的查询范围

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t total_features_found = 0;

        for (int i = 0; i < num_reads; ++i) {
            // 生成随机小范围查询
            double center_x = x_dis(gen);
            double center_y = y_dis(gen);
            double range = size_dis(gen);

            GisStorage::BBox query_bbox(center_x - range, center_y - range, center_x + range, center_y + range);

            auto result_ids = storage_system_->QueryByBBox(query_bbox);
            total_features_found += result_ids.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, total_features_found}; // 返回毫秒和找到的要素总数
    }

    // 测试基于bbox的并发随机读取指定数量的要素
    std::pair<double, size_t> testBBoxConcurrentRandomReadFeatures(int num_reads = 1000) {
        if (!isValid() || total_features_ == 0)
            return {-1.0, 0};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> x_dis(data_extent_.min_x, data_extent_.max_x);
        std::uniform_real_distribution<double> y_dis(data_extent_.min_y, data_extent_.max_y);
        std::uniform_real_distribution<double> size_dis(0.001, 0.01); // 很小的查询范围

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t total_features_found = 0;

        for (int i = 0; i < num_reads; ++i) {
            // 生成随机小范围查询
            double center_x = x_dis(gen);
            double center_y = y_dis(gen);
            double range = size_dis(gen);

            GisStorage::BBox query_bbox(center_x - range, center_y - range, center_x + range, center_y + range);

            auto result_ids = storage_system_->QueryByBBoxParallel(query_bbox);
            total_features_found += result_ids.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, total_features_found}; // 返回毫秒和找到的要素总数
    }

    // 单次空间查询并返回结果数量
    std::pair<double, size_t> testSingleSpatialQuery(double min_x, double min_y, double max_x, double max_y) {
        if (!isValid())
            return {-1.0, 0};

        // 创建查询边界框
        GisStorage::BBox query_bbox(min_x, min_y, max_x, max_y);

        auto start_time = std::chrono::high_resolution_clock::now();

        // 使用S2索引进行空间查询
        auto result_ids = storage_system_->QueryS2Index(query_bbox);
        size_t count = result_ids.size();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, count}; // 返回毫秒和结果数量
    }

    // 测试空间查询 - 随机边界框查询
    double testRandomSpatialQuery(int num_queries = 100) {
        if (!isValid())
            return -1.0;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> x_dis(data_extent_.min_x, data_extent_.max_x);
        std::uniform_real_distribution<double> y_dis(data_extent_.min_y, data_extent_.max_y);

        // 随机查询范围大小（相对于数据范围的百分比）
        std::uniform_real_distribution<double> size_dis(0.01, 0.1); // 1% 到 10%

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            // 生成随机查询范围
            double center_x = x_dis(gen);
            double center_y = y_dis(gen);
            double range_factor = size_dis(gen);

            double range_x = (data_extent_.max_x - data_extent_.min_x) * range_factor;
            double range_y = (data_extent_.max_y - data_extent_.min_y) * range_factor;

            double min_x = center_x - range_x / 2;
            double max_x = center_x + range_x / 2;
            double min_y = center_y - range_y / 2;
            double max_y = center_y + range_y / 2;

            // 确保在数据范围内
            min_x = std::max(min_x, data_extent_.min_x);
            max_x = std::min(max_x, data_extent_.max_x);
            min_y = std::max(min_y, data_extent_.min_y);
            max_y = std::min(max_y, data_extent_.max_y);

            // 执行空间查询
            GisStorage::BBox query_bbox(min_x, min_y, max_x, max_y);
            auto result_ids = storage_system_->QueryS2Index(query_bbox);
            total_results += result_ids.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试属性查询 - 精确匹配（使用高效方法）
    double testAttributeQueryExact(const std::string& field_name, const std::string& value, int num_queries = 1) {
        if (!isValid())
            return -1.0;

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            auto result_ids = storage_system_->QueryByAttributeEfficient(field_name, value);
            total_results += result_ids.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试属性查询 - 模式匹配
    double testAttributeQueryPattern(const std::string& field_name, const std::string& pattern, int num_queries = 100) {
        if (!isValid())
            return -1.0;

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            auto result_ids = storage_system_->QueryByAttributePattern(field_name, pattern);
            total_results += result_ids.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试复合查询 - 空间+属性（使用高效方法）
    double testSpatialAttributeQuery(double min_x, double min_y, double max_x, double max_y, const std::string& field_name, const std::string& value, int num_queries = 1) {
        if (!isValid())
            return -1.0;

        // 创建空间查询边界框
        GisStorage::BBox query_bbox(min_x, min_y, max_x, max_y);

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            // 使用高效的复合查询方法
            auto results = storage_system_->QuerySpatialAttributeEfficient(query_bbox, field_name, value);
            total_results += results.size();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 获取字段信息
    std::vector<std::string> getFieldNames() {
        std::vector<std::string> field_names;
        if (!isValid())
            return field_names;

        const auto& metadata = storage_system_->GetMetadata();
        for (const auto& [field_name, field_type] : metadata.field_definitions) {
            field_names.push_back(field_name);
        }

        return field_names;
    }

    // 获取字段值的样本
    std::vector<std::string> getFieldValueSamples(const std::string& field_name, int max_samples = 10) {
        std::vector<std::string> samples;
        if (!isValid())
            return samples;

        auto all_values = storage_system_->QueryAttributeValues(field_name);
        for (size_t i = 0; i < std::min(static_cast<size_t>(max_samples), all_values.size()); ++i) {
            samples.push_back(all_values[i].second);
        }

        return samples;
    }

    // 格式化文件大小
    std::string formatFileSize(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit < 3) {
            size /= 1024.0;
            unit++;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    }

    // 获取存储统计信息
    GisStorage::GisStorageSystem::StorageStats getStorageStats() {
        if (!isValid())
            return GisStorage::GisStorageSystem::StorageStats();

        return storage_system_->GetStorageStats();
    }
};

// Google Test 测试用例

class CustomFormatPerformanceTestFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // 设置测试用的自定义格式数据路径
        data_dir = "/home/chenming/Projects/geotalk-jni/src/main/jni/data/integrated_test";
        dataset_name = "td_gtbhdc_bg_530000_2020";

        // 检查数据文件是否存在
        std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
        if (!std::filesystem::exists(metadata_file)) {
            // 尝试其他可能的数据集
            dataset_name = "DLTB_2021CG";
            metadata_file = data_dir + "/" + dataset_name + "_meta.json";
        }
    }

    std::string data_dir;
    std::string dataset_name;
};

TEST_F(CustomFormatPerformanceTestFixture, CustomFormatExists) {
    // 检查测试文件是否存在
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    EXPECT_TRUE(std::filesystem::exists(metadata_file)) << "测试元数据文件不存在: " << metadata_file;

    // 检查其他必要文件
    std::vector<std::string> required_files = {data_dir + "/" + dataset_name + ".geom", data_dir + "/" + dataset_name + ".attr", data_dir + "/" + dataset_name + ".s2idx"};

    for (const auto& file : required_files) {
        EXPECT_TRUE(std::filesystem::exists(file)) << "测试文件不存在: " << file;
    }
}

TEST_F(CustomFormatPerformanceTestFixture, CustomFormatValid) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    EXPECT_TRUE(test.isValid()) << "无法打开自定义格式数据: " << dataset_name;

    if (test.isValid()) {
        std::cout << "自定义格式数据信息:" << std::endl;
        std::cout << "  数据集名称: " << dataset_name << std::endl;
        std::cout << "  总要素数: " << test.getTotalFeatures() << std::endl;

        const auto& extent = test.getDataExtent();
        std::cout << "  数据范围: [" << std::fixed << std::setprecision(6) << extent.min_x << ", " << extent.min_y << " - " << extent.max_x << ", " << extent.max_y << "]" << std::endl;

        auto field_names = test.getFieldNames();
        std::cout << "  字段数量: " << field_names.size() << std::endl;
        if (!field_names.empty()) {
            std::cout << "  字段列表: ";
            for (size_t i = 0; i < std::min(field_names.size(), size_t(5)); ++i) {
                std::cout << field_names[i];
                if (i < std::min(field_names.size(), size_t(5)) - 1) {
                    std::cout << ", ";
                }
            }
            if (field_names.size() > 5) {
                std::cout << " ...";
            }
            std::cout << std::endl;
        }

        // 显示存储统计信息（参考demo中的逻辑）
        auto stats = test.getStorageStats();
        std::cout << "  存储统计:" << std::endl;
        std::cout << "    几何数据: " << test.formatFileSize(stats.geometry_size_bytes) << std::endl;
        std::cout << "    属性数据: " << test.formatFileSize(stats.attribute_size_bytes) << std::endl;
        std::cout << "    字符串池: " << test.formatFileSize(stats.string_pool_size_bytes) << std::endl;
        std::cout << "    索引数据: " << test.formatFileSize(stats.index_size_bytes) << std::endl;
        std::cout << "    S2索引: " << test.formatFileSize(stats.s2_index_size_bytes) << std::endl;
        std::cout << "    总大小: " << test.formatFileSize(stats.total_size_bytes) << std::endl;

        // 显示生成的文件列表（参考demo中的逻辑）
        std::cout << "  生成的文件:" << std::endl;
        std::vector<std::pair<std::string, std::string>> files = {{"几何数据", data_dir + "/" + dataset_name + ".geom"},
                                                                  {"属性数据", data_dir + "/" + dataset_name + ".attr"},
                                                                  {"字符串池", data_dir + "/" + dataset_name + ".pool"},
                                                                  {"索引数据", data_dir + "/" + dataset_name + ".idx"},
                                                                  {"元数据", data_dir + "/" + dataset_name + "_meta.json"},
                                                                  {"S2索引", data_dir + "/" + dataset_name + ".s2idx"}};

        for (const auto& [type, path] : files) {
            if (std::filesystem::exists(path)) {
                auto size = std::filesystem::file_size(path);
                std::cout << "    " << type << ": " << std::filesystem::path(path).filename() << " (" << test.formatFileSize(size) << ")" << std::endl;
            } else {
                std::cout << "    " << type << ": " << std::filesystem::path(path).filename() << " (文件不存在)" << std::endl;
            }
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, SequentialReadPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    double read_time = test.testSequentialReadAll();
    EXPECT_GT(read_time, 0) << "基于bbox的顺序读取失败";

    std::cout << "基于bbox的顺序读取性能测试:" << std::endl;
    std::cout << "  查询时间: " << read_time << " ms" << std::endl;
    std::cout << "  总要素数: " << test.getTotalFeatures() << std::endl;

    if (read_time > 0 && test.getTotalFeatures() > 0) {
        double features_per_second = test.getTotalFeatures() / (read_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
    }
}

TEST_F(CustomFormatPerformanceTestFixture, S2IndexSequentialReadPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    double read_time = test.testS2IndexSequentialReadAll();
    EXPECT_GT(read_time, 0) << "顺序读取失败";

    std::cout << "顺序读取性能测试 (轻量级模式 - S2索引查询):" << std::endl;
    std::cout << "  查询时间: " << read_time << " ms" << std::endl;
    std::cout << "  总要素数: " << test.getTotalFeatures() << std::endl;

    if (read_time > 0 && test.getTotalFeatures() > 0) {
        double features_per_second = test.getTotalFeatures() / (read_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
    }
}

TEST_F(CustomFormatPerformanceTestFixture, S2IndexRandomReadPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    int num_reads = 1000;
    auto [read_time, features_found] = test.testS2IndexRandomReadFeatures(num_reads);
    EXPECT_GT(read_time, 0) << "随机读取失败";

    std::cout << "随机读取性能测试 (轻量级模式 - 随机S2查询, " << num_reads << "次):" << std::endl;
    std::cout << "  查询时间: " << read_time << " ms" << std::endl;
    std::cout << "  找到要素总数: " << features_found << std::endl;

    if (read_time > 0) {
        double queries_per_second = num_reads / (read_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;

        if (features_found > 0) {
            double features_per_second = features_found / (read_time / 1000.0);
            std::cout << "  要素查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, BBoxRandomReadPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    int num_reads = 1000;
    auto [read_time, features_found] = test.testBBoxRandomReadFeatures(num_reads);
    EXPECT_GT(read_time, 0) << "基于bbox的随机读取失败";

    std::cout << "基于bbox的随机读取性能测试 (" << num_reads << "次):" << std::endl;
    std::cout << "  查询时间: " << read_time << " ms" << std::endl;
    std::cout << "  找到要素总数: " << features_found << std::endl;

    if (read_time > 0) {
        double queries_per_second = num_reads / (read_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;

        if (features_found > 0) {
            double features_per_second = features_found / (read_time / 1000.0);
            std::cout << "  要素查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, BBoxConcurrentRandomReadPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    int num_reads = 1000;
    auto [read_time, features_found] = test.testBBoxConcurrentRandomReadFeatures(num_reads);
    EXPECT_GT(read_time, 0) << "基于bbox的并发随机读取失败";

    std::cout << "基于bbox的并发随机读取性能测试 (" << num_reads << "次):" << std::endl;
    std::cout << "  查询时间: " << read_time << " ms" << std::endl;
    std::cout << "  找到要素总数: " << features_found << std::endl;

    if (read_time > 0) {
        double queries_per_second = num_reads / (read_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;

        if (features_found > 0) {
            double features_per_second = features_found / (read_time / 1000.0);
            std::cout << "  要素查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, SpatialQueryPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    const auto& extent = test.getDataExtent();

    // 云南全省测试区域（与demo保持一致）
    std::vector<std::pair<std::string, std::pair<double, double>>> test_regions = {
      {"昆明市区", {102.7, 25.0}}, // 昆明市中心
      {"大理地区", {100.2, 25.6}}, // 大理
      {"西双版纳", {100.8, 22.0}}, // 西双版纳
      {"丽江地区", {100.2, 26.9}}, // 丽江
      {"曲靖地区", {103.8, 25.5}}  // 曲靖
    };

    // 定义不同查询范围大小（模拟不同缩放级别）
    std::vector<std::pair<std::string, double>> zoom_levels = {
      {"省级视图", 2.0},   // 大范围查询
      {"地区级视图", 0.5}, // 中等范围查询
      {"县级视图", 0.1},   // 小范围查询
      {"乡镇级视图", 0.02} // 很小范围查询
    };

    for (const auto& [zoom_name, range] : zoom_levels) {
        std::cout << "\n  " << zoom_name << " 查询测试 (范围: ±" << range << "°):" << std::endl;

        size_t total_features = 0;
        size_t successful_queries = 0;
        auto start_time = std::chrono::high_resolution_clock::now();

        // 测试前3个地区
        size_t test_count = std::min(static_cast<size_t>(3), test_regions.size());

        for (size_t i = 0; i < test_count; ++i) {
            const auto& [region_name, center] = test_regions[i];
            auto [center_lng, center_lat] = center;

            // 创建查询bbox
            double min_lng = center_lng - range;
            double max_lng = center_lng + range;
            double min_lat = center_lat - range;
            double max_lat = center_lat + range;

            // 确保bbox在数据范围内
            min_lng = std::max(min_lng, extent.min_x);
            max_lng = std::min(max_lng, extent.max_x);
            min_lat = std::max(min_lat, extent.min_y);
            max_lat = std::min(max_lat, extent.max_y);

            // 执行单次查询并统计结果
            auto [query_time, count] = test.testSingleSpatialQuery(min_lng, min_lat, max_lng, max_lat);

            total_features += count;
            if (count > 0) {
                successful_queries++;
            }

            std::cout << "    " << region_name << " [" << std::fixed << std::setprecision(4) << min_lng << "," << min_lat << " - " << max_lng << "," << max_lat << "]: " << count << " 个要素" << std::endl;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double query_time = std::chrono::duration<double>(end_time - start_time).count() * 1000.0; // 转换为毫秒

        std::cout << "    测试区域: " << test_count << " 个" << std::endl;
        std::cout << "    成功查询: " << successful_queries << " 个" << std::endl;
        std::cout << "    查询要素总数: " << total_features << std::endl;
        std::cout << "    查询时间: " << std::fixed << std::setprecision(3) << query_time << " ms" << std::endl;
        if (test_count > 0) {
            std::cout << "    平均每区域查询时间: " << std::fixed << std::setprecision(3) << query_time / test_count << " ms" << std::endl;
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, AttributeQueryPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    auto field_names = test.getFieldNames();
    if (field_names.empty()) {
        // 在轻量级模式下，字段信息可能不在元数据中
        // 我们直接尝试查询dlbm字段
        std::cout << "元数据中没有字段信息，直接尝试查询dlbm字段" << std::endl;
    }

    // 优先使用 dlbm 字段进行测试
    std::string test_field = "dlbm";
    std::string test_value = "0101";

    // 检查 dlbm 字段是否存在
    bool dlbm_exists = false;
    if (!field_names.empty()) {
        for (const auto& field : field_names) {
            if (field == "dlbm") {
                dlbm_exists = true;
                break;
            }
        }
    }

    if (!dlbm_exists && !field_names.empty()) {
        // 如果 dlbm 字段不存在，使用第一个字段
        test_field = field_names[0];
        auto samples = test.getFieldValueSamples(test_field, 5);
        if (samples.empty()) {
            GTEST_SKIP() << "字段 '" << test_field << "' 没有有效值，跳过测试";
        }
        test_value = samples[0];
    }
    // 如果field_names为空，我们直接使用dlbm字段进行测试

    std::cout << "使用字段 '" << test_field << "' 进行属性查询测试" << std::endl;
    std::cout << "使用值 '" << test_value << "' 进行精确匹配测试" << std::endl;

    // 精确匹配查询（使用高效方法）
    int num_queries = 1; // 只测试一次，因为高效查询会遍历所有数据
    double exact_time = test.testAttributeQueryExact(test_field, test_value, num_queries);
    EXPECT_GT(exact_time, 0) << "精确匹配查询失败";

    std::cout << "属性查询性能测试:" << std::endl;
    std::cout << "  精确匹配查询 (" << num_queries << "次): " << exact_time << " ms" << std::endl;

    if (exact_time > 0) {
        double queries_per_second = num_queries / (exact_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;
    }

    // 模式匹配查询（使用值的前几个字符）
    if (test_value.length() > 2) {
        std::string pattern = test_value.substr(0, 2);
        double pattern_time = test.testAttributeQueryPattern(test_field, pattern, num_queries);
        EXPECT_GT(pattern_time, 0) << "模式匹配查询失败";

        std::cout << "  模式匹配查询 (" << num_queries << "次, 模式: '" << pattern << "'): " << pattern_time << " ms" << std::endl;

        if (pattern_time > 0) {
            double queries_per_second = num_queries / (pattern_time / 1000.0);
            std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;
        }
    }
}

TEST_F(CustomFormatPerformanceTestFixture, CompositeQueryPerformance) {
    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    auto field_names = test.getFieldNames();
    if (field_names.empty()) {
        // 在轻量级模式下，字段信息可能不在元数据中
        // 我们直接尝试查询dlbm字段
        std::cout << "元数据中没有字段信息，直接尝试查询dlbm字段" << std::endl;
    }

    const auto& extent = test.getDataExtent();

    // 创建中等大小的查询范围
    double range_factor = 0.05; // 5% 的数据范围
    double range_x = (extent.max_x - extent.min_x) * range_factor;
    double range_y = (extent.max_y - extent.min_y) * range_factor;

    double center_x = (extent.min_x + extent.max_x) / 2;
    double center_y = (extent.min_y + extent.max_y) / 2;

    double min_x = center_x - range_x / 2;
    double max_x = center_x + range_x / 2;
    double min_y = center_y - range_y / 2;
    double max_y = center_y + range_y / 2;

    // 选择字段进行测试
    std::string test_field = "dlbm";
    std::string test_value = "0101";

    if (!field_names.empty()) {
        // 如果元数据中有字段信息，使用第一个字段
        test_field = field_names[0];
        auto samples = test.getFieldValueSamples(test_field, 1);
        if (!samples.empty()) {
            test_value = samples[0];
        }
    }
    int num_queries = 1; // 只测试一次，因为高效查询会处理所有空间候选要素

    double composite_time = test.testSpatialAttributeQuery(min_x, min_y, max_x, max_y, test_field, test_value, num_queries);
    EXPECT_GT(composite_time, 0) << "复合查询失败";

    std::cout << "复合查询性能测试 (空间+属性, 高效批量处理):" << std::endl;
    std::cout << "  查询时间: " << composite_time << " ms" << std::endl;
    std::cout << "  空间范围: [" << std::fixed << std::setprecision(6) << min_x << ", " << min_y << " - " << max_x << ", " << max_y << "]" << std::endl;
    std::cout << "  属性条件: " << test_field << " = '" << test_value << "'" << std::endl;

    if (composite_time > 0) {
        double queries_per_second = num_queries / (composite_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;
    }
}

// 参数化测试示例 - 不同查询次数
class QueryCountTest : public ::testing::TestWithParam<int> {};

TEST_P(QueryCountTest, RandomSpatialQueryWithDifferentCounts) {
    std::string data_dir = "/home/chenming/Projects/test/s2-test/output_data/integrated_test";
    std::string dataset_name = "td_gtbhdc_bg_530000_2020";

    std::string metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    if (!std::filesystem::exists(metadata_file)) {
        dataset_name = "DLTB_2021CG";
        metadata_file = data_dir + "/" + dataset_name + "_meta.json";
    }

    if (!std::filesystem::exists(metadata_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    CustomFormatPerformanceTest test(data_dir, dataset_name);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开自定义格式数据，跳过测试";
    }

    int num_queries = GetParam();
    double query_time = test.testRandomSpatialQuery(num_queries);

    EXPECT_GT(query_time, 0) << "随机空间查询失败 (查询次数=" << num_queries << ")";

    std::cout << "查询次数 " << num_queries << " 的随机空间查询:" << std::endl;
    std::cout << "  查询时间: " << query_time << " ms" << std::endl;

    if (query_time > 0) {
        double queries_per_second = num_queries / (query_time / 1000.0);
        std::cout << "  查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;
    }
}

INSTANTIATE_TEST_SUITE_P(QueryCounts, QueryCountTest, ::testing::Values(10, 50, 100, 200, 500));
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
