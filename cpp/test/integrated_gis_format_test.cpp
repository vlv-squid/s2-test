#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
#include <iomanip>

#include <ogrsf_frmts.h>

// GIS存储模块
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/shapefile_converter.h"
#include "gisstorage/geometry_data.h"
#include "gisstorage/attribute_data.h"

// GIS索引模块
#include "gisindex/s2spatial_index.h"

// 测试配置
const std::string TEST_DATA_PATH = "../data/test.shp";
const std::string OUTPUT_DIR = "./test_output/integrated_test";
const std::string INDEX_DIR = "./test_output/integrated_test";

class IntegratedGisFormatTest {
  private:
    std::unique_ptr<GisStorage::GisStorageSystem> storage_system_;
    std::unique_ptr<S2Main::S2SpatialIndex> spatial_index_;

    // 文件扩展名定义
    struct FileExtensions {
        static constexpr const char* GEOMETRY_DATA = ".geom";  // 几何数据文件
        static constexpr const char* ATTRIBUTE_DATA = ".attr"; // 属性数据文件
        static constexpr const char* STRING_POOL = ".pool";    // 字符串池文件
        static constexpr const char* INDEX_DATA = ".idx";      // 索引数据文件
        static constexpr const char* METADATA = ".meta";       // 元数据文件
        static constexpr const char* S2_INDEX = ".s2idx";      // S2空间索引文件
    };

  public:
    IntegratedGisFormatTest() {
        // 创建输出目录
        std::filesystem::create_directories(OUTPUT_DIR);
        std::filesystem::create_directories(INDEX_DIR);

        // 初始化存储系统
        storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(OUTPUT_DIR);

        // 初始化S2空间索引 - 统一命名格式
        std::string s2_index_path = INDEX_DIR + "/test" + FileExtensions::S2_INDEX;
        spatial_index_ = std::make_unique<S2Main::S2SpatialIndex>(s2_index_path, 15);
    }

    // 测试文件扩展名定义
    void testFileExtensions() {
        std::cout << "=== 测试文件扩展名定义 ===" << std::endl;
        std::cout << "几何数据文件: " << FileExtensions::GEOMETRY_DATA << std::endl;
        std::cout << "属性数据文件: " << FileExtensions::ATTRIBUTE_DATA << std::endl;
        std::cout << "字符串池文件: " << FileExtensions::STRING_POOL << std::endl;
        std::cout << "索引数据文件: " << FileExtensions::INDEX_DATA << std::endl;
        std::cout << "元数据文件: " << FileExtensions::METADATA << std::endl;
        std::cout << "S2空间索引文件: " << FileExtensions::S2_INDEX << std::endl;
        std::cout << std::endl;
    }

    // 测试Shapefile转换和存储
    void testShapefileConversion() {
        std::cout << "=== 测试Shapefile转换和存储 ===" << std::endl;

        if (!std::filesystem::exists(TEST_DATA_PATH)) {
            std::cout << "测试数据文件不存在: " << TEST_DATA_PATH << std::endl;
            return;
        }

        try {
            // 使用ShapefileConverter进行实际转换
            std::cout << "开始转换Shapefile..." << std::endl;
            GisStorage::ShapefileConverter converter(TEST_DATA_PATH, OUTPUT_DIR);

            auto feature_ids = converter.convert();
            std::cout << "✓ Shapefile转换成功，转换了 " << feature_ids.size() << " 个要素" << std::endl;

            // 获取转换统计信息
            auto stats = converter.getConversionStats();
            std::cout << "  总要素数: " << stats.total_features << std::endl;
            std::cout << "  有效要素数: " << stats.valid_features << std::endl;
            std::cout << "  几何数据大小: " << stats.geometry_size << " 字节" << std::endl;
            std::cout << "  属性数据压缩比: " << std::fixed << std::setprecision(2) << stats.compression_ratio << "%" << std::endl;
            std::cout << "  转换耗时: " << std::fixed << std::setprecision(2) << stats.conversion_time_seconds << " 秒" << std::endl;

            // 重新初始化存储系统以使用转换后的文件
            storage_system_->initializeStorageFiles(TEST_DATA_PATH);
            std::cout << "✓ 存储文件初始化成功" << std::endl;

            // 验证生成的文件
            std::string base_name = std::filesystem::path(TEST_DATA_PATH).stem().string();
            std::vector<std::string> expected_files = {converter.getGeometryFilePath(), converter.getAttributeFilePath(), converter.getStringPoolFilePath(), converter.getIndexFilePath()};

            for (const auto& file : expected_files) {
                if (std::filesystem::exists(file)) {
                    std::cout << "✓ 文件生成成功: " << std::filesystem::path(file).filename() << std::endl;
                } else {
                    std::cout << "✗ 文件缺失: " << std::filesystem::path(file).filename() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "✗ Shapefile转换失败: " << e.what() << std::endl;
        }
        std::cout << std::endl;
    }

    // 测试S2空间索引构建
    void testS2SpatialIndex() {
        std::cout << "=== 测试S2空间索引构建 ===" << std::endl;

        try {
            // 从数据集构建索引
            bool success = spatial_index_->buildFromDataset(TEST_DATA_PATH, 1000);
            if (success) {
                std::cout << "✓ S2空间索引构建成功" << std::endl;
                std::cout << "  索引大小: " << spatial_index_->getIndexSize() << " 个S2单元格" << std::endl;
                std::cout << "  要素总数: " << spatial_index_->getTotalFeatureCount() << std::endl;

                // 保存索引
                spatial_index_->save();
                std::cout << "✓ S2空间索引保存成功" << std::endl;
            } else {
                std::cout << "✗ S2空间索引构建失败" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "✗ S2空间索引测试失败: " << e.what() << std::endl;
        }
        std::cout << std::endl;
    }

    // 测试数据读取和索引查询的整合
    void testIntegratedDataAccess() {
        std::cout << "=== 测试整合数据访问 ===" << std::endl;

        try {
            // 获取所有要素ID
            auto feature_ids = storage_system_->getAllFeatureIds();
            if (feature_ids.empty()) {
                std::cout << "✗ 没有可用的要素ID" << std::endl;
                return;
            }

            std::cout << "✓ 获取到 " << feature_ids.size() << " 个要素ID" << std::endl;

            // 测试几何数据读取
            if (!feature_ids.empty()) {
                auto geometry = storage_system_->readGeometry(feature_ids[0]);
                if (geometry) {
                    std::cout << "✓ 几何数据读取成功，要素ID: " << feature_ids[0] << std::endl;
                    std::cout << "  几何类型: " << static_cast<int>(geometry->getGeometryType()) << std::endl;
                    std::cout << "  坐标数量: " << geometry->getCoordinates().size() << std::endl;
                } else {
                    std::cout << "✗ 几何数据读取失败" << std::endl;
                }
            }

            // 测试属性数据读取
            if (!feature_ids.empty()) {
                auto attributes = storage_system_->readAttribute(feature_ids[0]);
                if (attributes) {
                    std::cout << "✓ 属性数据读取成功，要素ID: " << feature_ids[0] << std::endl;
                    std::cout << "  属性字段数量: " << attributes->getProperties().size() << std::endl;
                } else {
                    std::cout << "✗ 属性数据读取失败" << std::endl;
                }
            }

            // 测试空间查询
            if (spatial_index_->isIndexValid()) {
                // 创建一个测试查询范围
                S2LatLngRect query_rect(S2LatLng::FromDegrees(26.0, 103.0), S2LatLng::FromDegrees(27.0, 104.0));

                auto results = spatial_index_->query(query_rect, 16);
                std::cout << "✓ 空间查询成功，查询结果数量: " << results.size() << std::endl;
            }

        } catch (const std::exception& e) {
            std::cout << "✗ 整合数据访问测试失败: " << e.what() << std::endl;
        }
        std::cout << std::endl;
    }

    // 运行所有测试
    void runAllTests() {
        std::cout << "开始运行整合GIS格式测试..." << std::endl;
        std::cout << "测试数据路径: " << TEST_DATA_PATH << std::endl;
        std::cout << "输出目录: " << OUTPUT_DIR << std::endl;
        std::cout << "索引目录: " << INDEX_DIR << std::endl;
        std::cout << std::endl;

        testFileExtensions();
        testShapefileConversion();
        testS2SpatialIndex();
        testIntegratedDataAccess();

        std::cout << "整合GIS格式测试完成!" << std::endl;
    }
};

int main() {
    // 初始化GDAL
    GDALAllRegister();

    try {
        IntegratedGisFormatTest test;
        test.runAllTests();
    } catch (const std::exception& e) {
        std::cerr << "测试执行失败: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
