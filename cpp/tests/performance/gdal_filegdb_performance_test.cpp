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

// GDAL 头文件
#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogrsf_frmts.h>
#include <ogr_geometry.h>

class GdalFileGdbPerformanceTest {
  private:
    std::string filegdb_path_;
    GDALDataset* dataset_;
    OGRLayer* layer_;
    size_t total_features_;
    OGREnvelope data_extent_;

  public:
    GdalFileGdbPerformanceTest(const std::string& filegdb_path)
        : filegdb_path_(filegdb_path)
        , dataset_(nullptr)
        , layer_(nullptr)
        , total_features_(0) {
        // 初始化GDAL
        (void)GDALAllRegister();

        // 打开FileGDB
        dataset_ = static_cast<GDALDataset*>(GDALOpenEx(filegdb_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));

        if (dataset_) {
            layer_ = dataset_->GetLayer(0);
            if (layer_) {
                total_features_ = layer_->GetFeatureCount();
                (void)layer_->GetExtent(&data_extent_);
            }
        }
    }

    ~GdalFileGdbPerformanceTest() {
        if (dataset_) {
            GDALClose(dataset_);
        }
    }

    // 检查FileGDB是否有效
    bool isValid() const { return dataset_ != nullptr && layer_ != nullptr; }

    // 获取总要素数
    size_t getTotalFeatures() const { return total_features_; }

    // 获取数据范围
    const OGREnvelope& getDataExtent() const { return data_extent_; }

    // 测试顺序读取所有要素
    double testSequentialReadAll() {
        if (!isValid())
            return -1.0;

        layer_->ResetReading();

        auto start_time = std::chrono::high_resolution_clock::now();

        OGRFeature* feature;
        size_t count = 0;
        while ((feature = layer_->GetNextFeature()) != nullptr) {
            count++;
            OGRFeature::DestroyFeature(feature);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试随机读取指定数量的要素
    std::pair<double, size_t> testRandomReadFeatures(int num_reads = 1000) {
        if (!isValid() || total_features_ == 0)
            return {-1.0, 0};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, total_features_ - 1);

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t successful_reads = 0;

        for (int i = 0; i < num_reads; ++i) {
            size_t random_fid = dis(gen);
            OGRFeature* feature = layer_->GetFeature(random_fid);
            if (feature) {
                successful_reads++;
                OGRFeature::DestroyFeature(feature);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, successful_reads}; // 返回毫秒和成功读取的要素数
    }

    // 测试空间查询 - 边界框查询
    double testSpatialQueryBBox(double min_x, double min_y, double max_x, double max_y, int num_queries = 100) {
        if (!isValid())
            return -1.0;

        // 创建查询几何体
        OGRPolygon query_geom;
        OGRLinearRing ring;
        ring.addPoint(min_x, min_y);
        ring.addPoint(max_x, min_y);
        ring.addPoint(max_x, max_y);
        ring.addPoint(min_x, max_y);
        ring.addPoint(min_x, min_y);
        query_geom.addRing(&ring);

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            layer_->SetSpatialFilter(&query_geom);
            layer_->ResetReading();

            OGRFeature* feature;
            size_t count = 0;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                count++;
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        // 清除空间过滤器
        layer_->SetSpatialFilter(nullptr);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 单次空间查询并返回结果数量
    std::pair<double, size_t> testSingleSpatialQuery(double min_x, double min_y, double max_x, double max_y) {
        if (!isValid())
            return {-1.0, 0};

        // 创建查询几何体
        OGRPolygon query_geom;
        OGRLinearRing ring;
        ring.addPoint(min_x, min_y);
        ring.addPoint(max_x, min_y);
        ring.addPoint(max_x, max_y);
        ring.addPoint(min_x, max_y);
        ring.addPoint(min_x, min_y);
        query_geom.addRing(&ring);

        auto start_time = std::chrono::high_resolution_clock::now();

        layer_->SetSpatialFilter(&query_geom);
        layer_->ResetReading();

        OGRFeature* feature;
        size_t count = 0;
        while ((feature = layer_->GetNextFeature()) != nullptr) {
            count++;
            OGRFeature::DestroyFeature(feature);
        }

        // 清除空间过滤器
        layer_->SetSpatialFilter(nullptr);

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
        std::uniform_real_distribution<double> x_dis(data_extent_.MinX, data_extent_.MaxX);
        std::uniform_real_distribution<double> y_dis(data_extent_.MinY, data_extent_.MaxY);

        // 随机查询范围大小（相对于数据范围的百分比）
        std::uniform_real_distribution<double> size_dis(0.01, 0.1); // 1% 到 10%

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            // 生成随机查询范围
            double center_x = x_dis(gen);
            double center_y = y_dis(gen);
            double range_factor = size_dis(gen);

            double range_x = (data_extent_.MaxX - data_extent_.MinX) * range_factor;
            double range_y = (data_extent_.MaxY - data_extent_.MinY) * range_factor;

            double min_x = center_x - range_x / 2;
            double max_x = center_x + range_x / 2;
            double min_y = center_y - range_y / 2;
            double max_y = center_y + range_y / 2;

            // 确保在数据范围内
            min_x = std::max(min_x, data_extent_.MinX);
            max_x = std::min(max_x, data_extent_.MaxX);
            min_y = std::max(min_y, data_extent_.MinY);
            max_y = std::min(max_y, data_extent_.MaxY);

            // 执行空间查询
            OGRPolygon query_geom;
            OGRLinearRing ring;
            ring.addPoint(min_x, min_y);
            ring.addPoint(max_x, min_y);
            ring.addPoint(max_x, max_y);
            ring.addPoint(min_x, max_y);
            ring.addPoint(min_x, min_y);
            query_geom.addRing(&ring);

            layer_->SetSpatialFilter(&query_geom);
            layer_->ResetReading();

            OGRFeature* feature;
            size_t count = 0;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                count++;
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        // 清除空间过滤器
        layer_->SetSpatialFilter(nullptr);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试属性查询 - 精确匹配（使用GDAL属性过滤器）
    std::pair<double, size_t> testAttributeQueryExact(const std::string& field_name, const std::string& value, int num_queries = 10) {
        if (!isValid())
            return {-1.0, 0};

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t total_results = 0;

        for (int i = 0; i < num_queries; ++i) {
            // 设置属性过滤器
            std::string filter = field_name + " = '" + value + "'";
            layer_->SetAttributeFilter(filter.c_str());
            layer_->ResetReading();

            // 计算匹配的要素数量
            size_t count = 0;
            OGRFeature* feature;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                count++;
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        // 清除属性过滤器
        layer_->SetAttributeFilter(nullptr);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, total_results}; // 返回毫秒和结果总数
    }

    // 测试属性查询 - 精确匹配（传统方式，用于对比）
    double testAttributeQueryExactTraditional(const std::string& field_name, const std::string& value, int num_queries = 10) {
        if (!isValid())
            return -1.0;

        // 获取字段索引
        OGRFeatureDefn* defn = layer_->GetLayerDefn();
        int field_index = defn->GetFieldIndex(field_name.c_str());
        if (field_index == -1)
            return -1.0;

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            layer_->ResetReading();

            OGRFeature* feature;
            size_t count = 0;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                if (feature->IsFieldSet(field_index)) {
                    const char* field_value = feature->GetFieldAsString(field_index);
                    if (field_value && strcmp(field_value, value.c_str()) == 0) {
                        count++;
                    }
                }
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 测试属性查询 - 模式匹配（使用GDAL属性过滤器）
    std::pair<double, size_t> testAttributeQueryPattern(const std::string& field_name, const std::string& pattern, int num_queries = 10) {
        if (!isValid())
            return {-1.0, 0};

        auto start_time = std::chrono::high_resolution_clock::now();
        size_t total_results = 0;

        for (int i = 0; i < num_queries; ++i) {
            // 设置属性过滤器（使用LIKE操作符）
            std::string filter = field_name + " LIKE '%" + pattern + "%'";
            layer_->SetAttributeFilter(filter.c_str());
            layer_->ResetReading();

            // 计算匹配的要素数量
            size_t count = 0;
            OGRFeature* feature;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                count++;
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        // 清除属性过滤器
        layer_->SetAttributeFilter(nullptr);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return {duration.count() / 1000.0, total_results}; // 返回毫秒和结果总数
    }

    // 测试复合查询 - 空间+属性
    double testSpatialAttributeQuery(double min_x, double min_y, double max_x, double max_y, const std::string& field_name, const std::string& value, int num_queries = 100) {
        if (!isValid())
            return -1.0;

        // 获取字段索引
        OGRFeatureDefn* defn = layer_->GetLayerDefn();
        int field_index = defn->GetFieldIndex(field_name.c_str());
        if (field_index == -1)
            return -1.0;

        // 创建空间过滤器
        OGRPolygon query_geom;
        OGRLinearRing ring;
        ring.addPoint(min_x, min_y);
        ring.addPoint(max_x, min_y);
        ring.addPoint(max_x, max_y);
        ring.addPoint(min_x, max_y);
        ring.addPoint(min_x, min_y);
        query_geom.addRing(&ring);

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t total_results = 0;
        for (int i = 0; i < num_queries; ++i) {
            layer_->SetSpatialFilter(&query_geom);
            layer_->ResetReading();

            OGRFeature* feature;
            size_t count = 0;
            while ((feature = layer_->GetNextFeature()) != nullptr) {
                if (feature->IsFieldSet(field_index)) {
                    const char* field_value = feature->GetFieldAsString(field_index);
                    if (field_value && strcmp(field_value, value.c_str()) == 0) {
                        count++;
                    }
                }
                OGRFeature::DestroyFeature(feature);
            }
            total_results += count;
        }

        // 清除空间过滤器
        layer_->SetSpatialFilter(nullptr);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 获取字段信息
    std::vector<std::string> getFieldNames() {
        std::vector<std::string> field_names;
        if (!isValid())
            return field_names;

        OGRFeatureDefn* defn = layer_->GetLayerDefn();
        int field_count = defn->GetFieldCount();

        for (int i = 0; i < field_count; ++i) {
            OGRFieldDefn* field_defn = defn->GetFieldDefn(i);
            field_names.push_back(field_defn->GetNameRef());
        }

        return field_names;
    }

    // 获取字段值的样本
    std::vector<std::string> getFieldValueSamples(const std::string& field_name, int max_samples = 10) {
        std::vector<std::string> samples;
        if (!isValid())
            return samples;

        OGRFeatureDefn* defn = layer_->GetLayerDefn();
        int field_index = defn->GetFieldIndex(field_name.c_str());
        if (field_index == -1)
            return samples;

        layer_->ResetReading();
        OGRFeature* feature;
        int count = 0;

        while ((feature = layer_->GetNextFeature()) != nullptr && count < max_samples) {
            if (feature->IsFieldSet(field_index)) {
                const char* field_value = feature->GetFieldAsString(field_index);
                if (field_value) {
                    samples.push_back(field_value);
                    count++;
                }
            }
            OGRFeature::DestroyFeature(feature);
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

    // 获取文件大小统计信息
    struct FileStats {
        size_t total_size = 0;
        size_t file_count = 0;
    };

    FileStats getFileStats() {
        FileStats stats;
        if (!isValid()) {
            return stats;
        }

        try {
            // 如果是目录（FileGDB），计算目录下所有文件的大小
            if (std::filesystem::is_directory(filegdb_path_)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(filegdb_path_)) {
                    if (entry.is_regular_file()) {
                        stats.total_size += entry.file_size();
                        stats.file_count++;
                    }
                }
            } else {
                // 如果是单个文件
                if (std::filesystem::exists(filegdb_path_)) {
                    stats.total_size = std::filesystem::file_size(filegdb_path_);
                    stats.file_count = 1;
                }
            }
        } catch (const std::exception& e) {
            // 忽略文件系统访问错误
        }

        return stats;
    }
};

// Google Test 测试用例

class GdalFileGdbPerformanceTestFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // 设置测试用的FileGDB路径
        // 优先使用环境变量 GDAL_TEST_DATA_PATH
        const char* env_path = std::getenv("GDAL_TEST_DATA_PATH");
        if (env_path && std::filesystem::exists(env_path)) {
            filegdb_path = env_path;
        } else {
            // 默认路径
            filegdb_path = "/home/chenming/Data/GIS_DATA/filegdb/td_gtbhdc_bg_530000_2020.gdb";

            // 如果默认路径不存在，尝试其他路径
            if (!std::filesystem::exists(filegdb_path)) {
                filegdb_path = "/home/chenming/Data/GIS_DATA/filegdb/DLTB_2021CG.gdb";
            }

            if (!std::filesystem::exists(filegdb_path)) {
                filegdb_path = "/home/chenming/Projects/test/s2-test/output_data/test.gdb";
            }

            if (!std::filesystem::exists(filegdb_path)) {
                filegdb_path = "/home/chenming/Projects/test/s2-test/output_data/test.shp";
            }
        }
    }

    std::string filegdb_path;
};

TEST_F(GdalFileGdbPerformanceTestFixture, FileGdbExists) {
    // 检查测试文件是否存在
    EXPECT_TRUE(std::filesystem::exists(filegdb_path)) << "测试文件不存在: " << filegdb_path;
}

TEST_F(GdalFileGdbPerformanceTestFixture, FileGdbValid) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    EXPECT_TRUE(test.isValid()) << "无法打开FileGDB: " << filegdb_path;

    if (test.isValid()) {
        std::cout << "FileGDB信息:" << std::endl;
        std::cout << "  文件路径: " << filegdb_path << std::endl;
        std::cout << "  总要素数: " << test.getTotalFeatures() << std::endl;

        const auto& extent = test.getDataExtent();
        std::cout << "  数据范围: [" << std::fixed << std::setprecision(6) << extent.MinX << ", " << extent.MinY << " - " << extent.MaxX << ", " << extent.MaxY << "]" << std::endl;

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

        // 显示文件统计信息
        auto file_stats = test.getFileStats();
        std::cout << "  文件统计:" << std::endl;
        std::cout << "    总大小: " << test.formatFileSize(file_stats.total_size) << std::endl;
        std::cout << "    文件数量: " << file_stats.file_count << std::endl;

        // 显示文件类型
        if (std::filesystem::is_directory(filegdb_path)) {
            std::cout << "    文件类型: FileGDB (目录)" << std::endl;
        } else {
            std::string ext = std::filesystem::path(filegdb_path).extension().string();
            std::cout << "    文件类型: " << ext << " 文件" << std::endl;
        }
    }
}

TEST_F(GdalFileGdbPerformanceTestFixture, SequentialReadPerformance) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开FileGDB，跳过测试";
    }

    double read_time = test.testSequentialReadAll();
    EXPECT_GT(read_time, 0) << "顺序读取失败";

    std::cout << "顺序读取性能测试:" << std::endl;
    std::cout << "  读取时间: " << read_time << " ms" << std::endl;
    std::cout << "  总要素数: " << test.getTotalFeatures() << std::endl;

    if (read_time > 0 && test.getTotalFeatures() > 0) {
        double features_per_second = test.getTotalFeatures() / (read_time / 1000.0);
        std::cout << "  读取速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
    }
}

TEST_F(GdalFileGdbPerformanceTestFixture, RandomReadPerformance) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开FileGDB，跳过测试";
    }

    int num_reads = 1000;
    auto [read_time, successful_reads] = test.testRandomReadFeatures(num_reads);
    EXPECT_GT(read_time, 0) << "随机读取失败";

    std::cout << "随机读取性能测试 (" << num_reads << "次):" << std::endl;
    std::cout << "  读取时间: " << read_time << " ms" << std::endl;
    std::cout << "  成功读取要素数: " << successful_reads << std::endl;

    if (read_time > 0) {
        double reads_per_second = num_reads / (read_time / 1000.0);
        std::cout << "  读取速度: " << std::fixed << std::setprecision(0) << reads_per_second << " 次/秒" << std::endl;

        if (successful_reads > 0) {
            double features_per_second = successful_reads / (read_time / 1000.0);
            std::cout << "  要素读取速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }
    }
}

TEST_F(GdalFileGdbPerformanceTestFixture, SpatialQueryPerformance) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开FileGDB，跳过测试";
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
            min_lng = std::max(min_lng, extent.MinX);
            max_lng = std::min(max_lng, extent.MaxX);
            min_lat = std::max(min_lat, extent.MinY);
            max_lat = std::min(max_lat, extent.MaxY);

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

TEST_F(GdalFileGdbPerformanceTestFixture, AttributeQueryPerformance) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开FileGDB，跳过测试";
    }

    auto field_names = test.getFieldNames();
    if (field_names.empty()) {
        GTEST_SKIP() << "没有找到字段，跳过属性查询测试";
    }

    // 优先使用 dlbm 字段进行测试
    std::string test_field = "dlbm";
    std::string test_value = "0101";

    // 检查 dlbm 字段是否存在
    bool dlbm_exists = false;
    for (const auto& field : field_names) {
        if (field == "dlbm") {
            dlbm_exists = true;
            break;
        }
    }

    if (!dlbm_exists) {
        // 如果 dlbm 字段不存在，使用第一个字段
        test_field = field_names[0];
        auto samples = test.getFieldValueSamples(test_field, 5);
        if (samples.empty()) {
            GTEST_SKIP() << "字段 '" << test_field << "' 没有有效值，跳过测试";
        }
        test_value = samples[0];
    }

    std::cout << "使用字段 '" << test_field << "' 进行属性查询测试" << std::endl;
    std::cout << "使用值 '" << test_value << "' 进行精确匹配测试" << std::endl;

    // 精确匹配查询 - 使用GDAL属性过滤器
    int num_queries = 10;
    auto [exact_time, exact_results] = test.testAttributeQueryExact(test_field, test_value, num_queries);
    EXPECT_GT(exact_time, 0) << "属性过滤器精确匹配查询失败";

    // 精确匹配查询 - 传统方式（用于对比）
    double exact_time_traditional = test.testAttributeQueryExactTraditional(test_field, test_value, num_queries);
    EXPECT_GT(exact_time_traditional, 0) << "传统精确匹配查询失败";

    std::cout << "属性查询性能测试:" << std::endl;
    std::cout << "  属性过滤器精确匹配查询 (" << num_queries << "次): " << exact_time << " ms" << std::endl;
    std::cout << "  找到匹配要素总数: " << exact_results << std::endl;
    std::cout << "  传统精确匹配查询 (" << num_queries << "次): " << exact_time_traditional << " ms" << std::endl;

    if (exact_time > 0) {
        double queries_per_second = num_queries / (exact_time / 1000.0);
        std::cout << "  属性过滤器查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;

        if (exact_results > 0) {
            double features_per_second = exact_results / (exact_time / 1000.0);
            std::cout << "  要素查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }
    }

    if (exact_time_traditional > 0) {
        double queries_per_second = num_queries / (exact_time_traditional / 1000.0);
        std::cout << "  传统查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;
    }

    // 计算性能提升
    if (exact_time > 0 && exact_time_traditional > 0) {
        double speedup = exact_time_traditional / exact_time;
        std::cout << "  属性过滤器查询比传统查询快 " << std::fixed << std::setprecision(1) << speedup << " 倍" << std::endl;
    }

    // 模式匹配查询（使用值的前几个字符）
    if (test_value.length() > 2) {
        std::string pattern = test_value.substr(0, 2);
        auto [pattern_time, pattern_results] = test.testAttributeQueryPattern(test_field, pattern, num_queries);
        EXPECT_GT(pattern_time, 0) << "模式匹配查询失败";

        std::cout << "  属性过滤器模式匹配查询 (" << num_queries << "次, 模式: '" << pattern << "'): " << pattern_time << " ms" << std::endl;
        std::cout << "  找到匹配要素总数: " << pattern_results << std::endl;

        if (pattern_time > 0) {
            double queries_per_second = num_queries / (pattern_time / 1000.0);
            std::cout << "  模式匹配查询速度: " << std::fixed << std::setprecision(0) << queries_per_second << " 次/秒" << std::endl;

            if (pattern_results > 0) {
                double features_per_second = pattern_results / (pattern_time / 1000.0);
                std::cout << "  要素查询速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
            }
        }
    }
}

TEST_F(GdalFileGdbPerformanceTestFixture, CompositeQueryPerformance) {
    if (!std::filesystem::exists(filegdb_path)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    GdalFileGdbPerformanceTest test(filegdb_path);
    if (!test.isValid()) {
        GTEST_SKIP() << "无法打开FileGDB，跳过测试";
    }

    auto field_names = test.getFieldNames();
    if (field_names.empty()) {
        // 如果没有字段信息，直接尝试查询dlbm字段
        std::cout << "没有找到字段信息，直接尝试查询dlbm字段" << std::endl;
    }

    const auto& extent = test.getDataExtent();

    // 创建中等大小的查询范围
    double range_factor = 0.05; // 5% 的数据范围
    double range_x = (extent.MaxX - extent.MinX) * range_factor;
    double range_y = (extent.MaxY - extent.MinY) * range_factor;

    double center_x = (extent.MinX + extent.MaxX) / 2;
    double center_y = (extent.MinY + extent.MaxY) / 2;

    double min_x = center_x - range_x / 2;
    double max_x = center_x + range_x / 2;
    double min_y = center_y - range_y / 2;
    double max_y = center_y + range_y / 2;

    // 选择字段进行测试 - filegdb没有元数据信息，直接使用dlbm字段
    std::string test_field = "dlbm";
    std::string test_value = "0101";
    int num_queries = 1; // 只测试一次，与自定义格式保持一致

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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
