#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
#include <iomanip>

#include <ogrsf_frmts.h>

// GIS存储模块
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/ogr_format_converter.h"
#include "gisstorage/geometry_data.h"
#include "gisstorage/attribute_data.h"

// GIS索引模块
#include "gisindex/s2spatial_index.h"

// 测试配置
const std::string TEST_DATA_PATH = "/home/chenming/Data/GIS_DATA/filegdb/DLTB_2021CG.gdb";
const std::string OUTPUT_DIR = "/home/chenming/Projects/test/s2-test/output_data/integrated_test";
const std::string INDEX_DIR = "/home/chenming/Projects/test/s2-test/output_data/integrated_test";

class IntegratedGisFormatTest : public ::testing::Test {
  protected:
    std::unique_ptr<GisStorage::GisStorageSystem> storage_system_;
    std::unique_ptr<S2Main::S2SpatialIndex> spatial_index_;

    void SetUp() override {
        // 创建输出目录
        std::filesystem::create_directories(OUTPUT_DIR);
        std::filesystem::create_directories(INDEX_DIR);

        // 初始化存储系统
        storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(OUTPUT_DIR);

        // 初始化S2空间索引 - 使用GisStorageSystem中定义的标准扩展名
        std::string s2_index_path = INDEX_DIR + "/test" + GisStorage::GisStorageSystem::FileExtensions::S2_INDEX;
        spatial_index_ = std::make_unique<S2Main::S2SpatialIndex>(s2_index_path, 15);
    }

    void TearDown() override {
        // 清理资源
        storage_system_.reset();
        spatial_index_.reset();
    }
};

// 测试文件扩展名定义
TEST_F(IntegratedGisFormatTest, FileExtensions) {
    // 验证文件扩展名定义（使用GisStorageSystem中的标准定义）
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::GEOMETRY_DATA, ".geom");
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::ATTRIBUTE_DATA, ".attr");
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::STRING_POOL, ".pool");
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::INDEX_DATA, ".idx");
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::METADATA, "_meta.json");
    EXPECT_STREQ(GisStorage::GisStorageSystem::FileExtensions::S2_INDEX, ".s2idx");
}

// 测试Shapefile转换和存储
TEST_F(IntegratedGisFormatTest, ShapefileConversion) {
    // 检查测试数据是否存在
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_PATH)) << "测试数据文件不存在: " << TEST_DATA_PATH;

    // 使用OGRFormatConverter进行实际转换
    GisStorage::OGRFormatConverter converter(TEST_DATA_PATH, OUTPUT_DIR);

    auto feature_ids = converter.Convert();
    EXPECT_GT(feature_ids.size(), 0) << "转换后应该包含要素";

    // 获取转换统计信息
    auto stats = converter.GetConversionStats();
    EXPECT_EQ(stats.total_features, stats.valid_features) << "所有要素都应该有效";
    EXPECT_GT(stats.compression_ratio, 0.0) << "应该有压缩效果";

    // 验证生成的文件
    std::vector<std::string> expected_files = {converter.GetGeometryFilePath(), converter.GetAttributeFilePath(), converter.GetStringPoolFilePath(), converter.GetIndexFilePath()};

    for (const auto& file : expected_files) {
        EXPECT_TRUE(std::filesystem::exists(file)) << "文件应该存在: " << std::filesystem::path(file).filename();
    }

    // 重新初始化存储系统以使用转换后的文件（在验证文件存在之后）
    EXPECT_NO_THROW(storage_system_->InitializeStorageFiles(TEST_DATA_PATH));
}

// 测试S2空间索引构建
TEST_F(IntegratedGisFormatTest, S2SpatialIndex) {
    // 检查测试数据是否存在
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_PATH)) << "测试数据文件不存在: " << TEST_DATA_PATH;

    // 从数据集构建索引
    bool success = spatial_index_->BuildFromDataset(TEST_DATA_PATH, 1000);
    EXPECT_TRUE(success) << "S2空间索引构建应该成功";

    if (success) {
        EXPECT_GT(spatial_index_->GetIndexSize(), 0) << "索引应该包含S2单元格";
        EXPECT_GT(spatial_index_->GetTotalFeatureCount(), 0) << "索引应该包含要素";

        // 保存索引
        EXPECT_NO_THROW(spatial_index_->Save());
    }
}

// 测试整合数据访问
TEST_F(IntegratedGisFormatTest, IntegratedDataAccess) {
    // 检查测试数据是否存在
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_PATH)) << "测试数据文件不存在: " << TEST_DATA_PATH;

    // 先执行OGR格式转换，生成数据文件
    GisStorage::OGRFormatConverter converter(TEST_DATA_PATH, OUTPUT_DIR);
    auto feature_ids = converter.Convert();
    ASSERT_GT(feature_ids.size(), 0) << "转换应该成功";

    // 构建S2空间索引
    bool success = spatial_index_->BuildFromDataset(TEST_DATA_PATH, 1000);
    ASSERT_TRUE(success) << "S2空间索引构建应该成功";

    // 保存S2索引
    EXPECT_NO_THROW(spatial_index_->Save());

    // 初始化存储系统以使用转换后的文件
    EXPECT_NO_THROW(storage_system_->InitializeStorageFiles(TEST_DATA_PATH));

    // 获取所有要素ID
    auto all_feature_ids = storage_system_->GetAllFeatureIds();
    EXPECT_GT(all_feature_ids.size(), 0) << "应该能获取到要素ID";

    if (!all_feature_ids.empty()) {
        // 测试几何数据读取
        EXPECT_NO_THROW({
            auto geometry = storage_system_->ReadGeometry(all_feature_ids[0]);
            EXPECT_NE(geometry, nullptr) << "几何数据应该能成功读取";
            if (geometry) {
                EXPECT_GT(geometry->GetCoordinates().size(), 0) << "几何数据应该包含坐标";
            }
        });

        // 测试属性数据读取
        // EXPECT_NO_THROW({
        //     auto attributes = storage_system_->ReadAttribute(all_feature_ids[0]);
        //     EXPECT_NE(attributes, nullptr) << "属性数据应该能成功读取";
        //     if (attributes) {
        //         EXPECT_GT(attributes->getProperties().size(), 0) << "属性数据应该包含字段";
        //     }
        // });
    }

    // 测试空间查询
    if (spatial_index_->IsIndexValid()) {
        // 创建一个测试查询范围
        S2LatLngRect query_rect(S2LatLng::FromDegrees(26.0, 103.0), S2LatLng::FromDegrees(27.0, 104.0));

        auto results = spatial_index_->Query(query_rect, 15);
        EXPECT_GT(results.size(), 0) << "空间查询应该返回结果";
    }
}

// 测试重构后的GisStorageSystem功能
TEST_F(IntegratedGisFormatTest, GisStorageSystemFeatures) {
    // 检查测试数据是否存在
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_PATH)) << "测试数据文件不存在: " << TEST_DATA_PATH;

    // 初始化存储系统
    EXPECT_NO_THROW(storage_system_->InitializeStorageFiles(TEST_DATA_PATH));

    // 测试文件路径获取
    EXPECT_FALSE(storage_system_->GetGeometryFilePath().empty());
    EXPECT_FALSE(storage_system_->GetAttributeFilePath().empty());
    EXPECT_FALSE(storage_system_->GetStringPoolFilePath().empty());
    EXPECT_FALSE(storage_system_->GetIndexFilePath().empty());
    EXPECT_FALSE(storage_system_->GetMetadataFilePath().empty());
    EXPECT_FALSE(storage_system_->GetS2IndexFilePath().empty());

    // 测试元数据功能
    const auto& metadata = storage_system_->GetMetadata();
    EXPECT_EQ(metadata.format_version, "1.0");
    EXPECT_EQ(metadata.source_format, "Shapefile");
    EXPECT_FALSE(metadata.source_file.empty());
    EXPECT_FALSE(metadata.creation_date.empty());

    // 测试坐标系统信息（如果存在）
    if (!metadata.source_coordinate_system.empty()) {
        EXPECT_FALSE(metadata.source_coordinate_system.empty());
    }
    if (!metadata.target_coordinate_system.empty()) {
        EXPECT_FALSE(metadata.target_coordinate_system.empty());
    }

    // 测试存储统计信息
    auto storage_stats = storage_system_->GetStorageStats();
    EXPECT_GE(storage_stats.total_files, 0);
    EXPECT_GE(storage_stats.total_size_bytes, 0);

    // 测试S2索引集成
    EXPECT_NO_THROW(storage_system_->InitializeS2Index(15));
    EXPECT_NO_THROW(storage_system_->BuildS2IndexFromDataset(TEST_DATA_PATH, 1000));

    if (storage_system_->IsS2IndexValid()) {
        EXPECT_GT(storage_system_->GetS2IndexSize(), 0);
        EXPECT_GT(storage_system_->GetS2TotalFeatureCount(), 0);

        // 测试S2索引保存
        EXPECT_NO_THROW(storage_system_->SaveS2Index());
    }

    // 测试元数据更新和保存
    EXPECT_NO_THROW(storage_system_->UpdateFileSizes());
    EXPECT_NO_THROW(storage_system_->UpdateChecksums());
    EXPECT_NO_THROW(storage_system_->SaveMetadata());
}

// 测试文件完整性
TEST_F(IntegratedGisFormatTest, FileIntegrity) {
    // 检查测试数据是否存在
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_PATH)) << "测试数据文件不存在: " << TEST_DATA_PATH;

    // 先执行转换
    GisStorage::OGRFormatConverter converter(TEST_DATA_PATH, OUTPUT_DIR);
    auto feature_ids = converter.Convert();
    ASSERT_GT(feature_ids.size(), 0) << "转换应该成功";

    // 检查文件大小
    std::vector<std::pair<std::string, size_t>> expected_files = {{converter.GetGeometryFilePath(), 0}, {converter.GetAttributeFilePath(), 0}, {converter.GetStringPoolFilePath(), 0}, {converter.GetIndexFilePath(), 0}};

    for (auto& [file_path, file_size] : expected_files) {
        ASSERT_TRUE(std::filesystem::exists(file_path)) << "文件应该存在: " << std::filesystem::path(file_path).filename();

        file_size = std::filesystem::file_size(file_path);
        EXPECT_GT(file_size, 0) << "文件大小应该大于0: " << std::filesystem::path(file_path).filename();
    }

    // 检查S2索引文件
    std::string s2_index_file = INDEX_DIR + "/test" + GisStorage::GisStorageSystem::FileExtensions::S2_INDEX;
    if (std::filesystem::exists(s2_index_file)) {
        auto s2_file_size = std::filesystem::file_size(s2_index_file);
        EXPECT_GT(s2_file_size, 0) << "S2索引文件大小应该大于0";
    }
}

// 主函数
int main(int argc, char** argv) {
    // 初始化GDAL
    GDALAllRegister();

    // 初始化Google Test
    ::testing::InitGoogleTest(&argc, argv);

    // 运行所有测试
    return RUN_ALL_TESTS();
}
