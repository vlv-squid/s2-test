//
// GIS存储系统完整使用示例
// 演示如何一次性完成Shapefile转换、S2索引构建和元数据生成
//

#include <iostream>
#include <string>
#include <filesystem>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

// GIS存储模块
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/shapefile_converter.h"

// GIS索引模块
#include "gisindex/s2spatial_index.h"

// GDAL初始化
#include <ogrsf_frmts.h>

class GisStorageDemo {
  public:
    GisStorageDemo(const std::string& input_shapefile, const std::string& output_dir)
        : input_shapefile_(input_shapefile)
        , output_dir_(output_dir) {
        // 初始化GDAL
        GDALAllRegister();

        // 创建输出目录
        std::filesystem::create_directories(output_dir_);

        // 提取文件名（不含扩展名）
        std::filesystem::path path(input_shapefile_);
        dataset_name_ = path.stem().string();

        std::cout << "=== GIS存储系统完整演示 ===" << std::endl;
        std::cout << "输入文件: " << input_shapefile_ << std::endl;
        std::cout << "输出目录: " << output_dir_ << std::endl;
        std::cout << "数据集名称: " << dataset_name_ << std::endl;
        std::cout << std::endl;
    }

    // 执行完整的处理流程
    bool runCompleteWorkflow() {
        try {
            // 步骤1: 验证输入文件
            if (!validateInput()) {
                return false;
            }

            // 步骤2: 转换Shapefile
            if (!convertShapefile()) {
                return false;
            }

            // 步骤3: 初始化存储系统
            if (!initializeStorageSystem()) {
                return false;
            }

            // 步骤4: 构建S2空间索引
            if (!buildS2Index()) {
                return false;
            }

            // 步骤5: 生成和保存元数据
            if (!generateMetadata()) {
                return false;
            }

            // 步骤6: 验证结果
            if (!validateResults()) {
                return false;
            }

            // 步骤7: 数据访问演示
            if (!demonstrateDataAccess()) {
                return false;
            }

            // 步骤8: 显示统计信息
            displayStatistics();

            std::cout << "\n=== 处理完成！===" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "处理过程中发生错误: " << e.what() << std::endl;
            return false;
        }
    }

  private:
    std::string input_shapefile_;
    std::string output_dir_;
    std::string dataset_name_;
    std::unique_ptr<GisStorage::GisStorageSystem> storage_system_;
    std::unique_ptr<S2Main::S2SpatialIndex> s2_index_;

    // 统计信息
    struct ProcessingStats {
        size_t total_features = 0;
        size_t valid_features = 0;
        double conversion_time = 0.0;
        double index_build_time = 0.0;
        double total_time = 0.0;
        size_t geometry_size = 0;
        size_t attribute_size = 0;
        size_t index_size = 0;
        size_t s2_index_size = 0;
        double compression_ratio = 0.0;
    } stats_;

    // 步骤1: 验证输入文件
    bool validateInput() {
        std::cout << "步骤1: 验证输入文件..." << std::endl;

        if (!std::filesystem::exists(input_shapefile_)) {
            std::cerr << "错误: 输入文件不存在: " << input_shapefile_ << std::endl;
            return false;
        }

        // 检查文件大小
        auto file_size = std::filesystem::file_size(input_shapefile_);
        std::cout << "  文件大小: " << formatFileSize(file_size) << std::endl;

        // 使用GDAL检查文件信息
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(input_shapefile_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));

        if (!dataset) {
            std::cerr << "错误: 无法打开Shapefile" << std::endl;
            return false;
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            std::cerr << "错误: 无法获取图层" << std::endl;
            GDALClose(dataset);
            return false;
        }

        stats_.total_features = layer->GetFeatureCount();
        std::cout << "  要素数量: " << stats_.total_features << std::endl;

        // 获取坐标系统信息
        OGRSpatialReference* srs = layer->GetSpatialRef();
        if (srs) {
            char* wkt = nullptr;
            srs->exportToWkt(&wkt);
            std::cout << "  坐标系统: " << (wkt ? wkt : "未知") << std::endl;
            CPLFree(wkt);
        }

        GDALClose(dataset);
        std::cout << "  ✓ 输入文件验证通过" << std::endl;
        return true;
    }

    // 步骤2: 转换Shapefile
    bool convertShapefile() {
        std::cout << "\n步骤2: 转换Shapefile..." << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        try {
            GisStorage::ShapefileConverter converter(input_shapefile_, output_dir_);
            auto feature_ids = converter.convert();

            auto end_time = std::chrono::high_resolution_clock::now();
            stats_.conversion_time = std::chrono::duration<double>(end_time - start_time).count();

            stats_.valid_features = feature_ids.size();

            // 获取转换统计信息
            auto conversion_stats = converter.getConversionStats();
            stats_.compression_ratio = conversion_stats.compression_ratio;

            std::cout << "  ✓ 转换完成" << std::endl;
            std::cout << "  有效要素: " << stats_.valid_features << "/" << stats_.total_features << std::endl;
            std::cout << "  转换时间: " << std::fixed << std::setprecision(3) << stats_.conversion_time << " 秒" << std::endl;
            std::cout << "  压缩率: " << std::fixed << std::setprecision(2) << stats_.compression_ratio << "%" << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 转换失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤3: 初始化存储系统
    bool initializeStorageSystem() {
        std::cout << "\n步骤3: 初始化存储系统..." << std::endl;

        try {
            storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(output_dir_);
            storage_system_->initializeStorageFiles(input_shapefile_);

            std::cout << "  ✓ 存储系统初始化完成" << std::endl;
            std::cout << "  几何文件: " << storage_system_->getGeometryFilePath() << std::endl;
            std::cout << "  属性文件: " << storage_system_->getAttributeFilePath() << std::endl;
            std::cout << "  索引文件: " << storage_system_->getIndexFilePath() << std::endl;
            std::cout << "  元数据文件: " << storage_system_->getMetadataFilePath() << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 存储系统初始化失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤4: 构建S2空间索引
    bool buildS2Index() {
        std::cout << "\n步骤4: 构建S2空间索引..." << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        try {
            // 初始化S2索引
            storage_system_->initializeS2Index(15); // 使用分辨率15

            // 构建索引
            bool success = storage_system_->buildS2IndexFromDataset(input_shapefile_, 1000);

            auto end_time = std::chrono::high_resolution_clock::now();
            stats_.index_build_time = std::chrono::duration<double>(end_time - start_time).count();

            if (success) {
                stats_.s2_index_size = storage_system_->getS2IndexSize();
                std::cout << "  ✓ S2索引构建完成" << std::endl;
                std::cout << "  索引大小: " << storage_system_->getS2IndexSize() << " 个单元格" << std::endl;
                std::cout << "  要素数量: " << storage_system_->getS2TotalFeatureCount() << std::endl;
                std::cout << "  构建时间: " << std::fixed << std::setprecision(3) << stats_.index_build_time << " 秒" << std::endl;

                // 保存索引
                storage_system_->saveS2Index();
                std::cout << "  ✓ S2索引已保存" << std::endl;

                return true;
            } else {
                std::cerr << "  ✗ S2索引构建失败" << std::endl;
                return false;
            }

        } catch (const std::exception& e) {
            std::cerr << "  ✗ S2索引构建失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤5: 生成和保存元数据
    bool generateMetadata() {
        std::cout << "\n步骤5: 生成和保存元数据..." << std::endl;

        try {
            // 更新文件大小信息
            storage_system_->updateFileSizes();

            // 更新校验和
            storage_system_->updateChecksums();

            // 保存元数据
            storage_system_->saveMetadata();

            std::cout << "  ✓ 元数据生成完成" << std::endl;

            // 显示元数据信息
            const auto& metadata = storage_system_->getMetadata();
            std::cout << "  格式版本: " << metadata.format_version << std::endl;
            std::cout << "  源格式: " << metadata.source_format << std::endl;
            std::cout << "  创建时间: " << metadata.creation_date << std::endl;
            std::cout << "  总要素数: " << metadata.total_features << std::endl;
            std::cout << "  有效要素数: " << metadata.valid_features << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 元数据生成失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤6: 验证结果
    bool validateResults() {
        std::cout << "\n步骤6: 验证处理结果..." << std::endl;

        try {
            // 检查所有必需文件是否存在
            std::vector<std::string> required_files = {storage_system_->getGeometryFilePath(),
                                                       storage_system_->getAttributeFilePath(),
                                                       storage_system_->getStringPoolFilePath(),
                                                       storage_system_->getIndexFilePath(),
                                                       storage_system_->getMetadataFilePath(),
                                                       storage_system_->getS2IndexFilePath()};

            for (const auto& file : required_files) {
                if (!std::filesystem::exists(file)) {
                    std::cerr << "  ✗ 文件不存在: " << file << std::endl;
                    return false;
                }
            }

            // 获取文件大小统计
            auto storage_stats = storage_system_->getStorageStats();
            stats_.geometry_size = storage_stats.geometry_size_bytes;
            stats_.attribute_size = storage_stats.attribute_size_bytes;
            stats_.index_size = storage_stats.index_size_bytes;
            stats_.s2_index_size = storage_stats.s2_index_size_bytes;

            std::cout << "  ✓ 所有文件验证通过" << std::endl;
            std::cout << "  几何数据: " << formatFileSize(stats_.geometry_size) << std::endl;
            std::cout << "  属性数据: " << formatFileSize(stats_.attribute_size) << std::endl;
            std::cout << "  索引数据: " << formatFileSize(stats_.index_size) << std::endl;
            std::cout << "  S2索引: " << formatFileSize(stats_.s2_index_size) << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 结果验证失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤7: 数据访问演示
    bool demonstrateDataAccess() {
        std::cout << "\n步骤7: 数据访问演示..." << std::endl;

        try {
            // 获取所有要素ID
            auto all_feature_ids = storage_system_->getAllFeatureIds();
            if (all_feature_ids.empty()) {
                std::cerr << "  ✗ 没有找到任何要素" << std::endl;
                return false;
            }

            std::cout << "  总要素数: " << all_feature_ids.size() << std::endl;

            // 演示读取前几个要素
            size_t demo_count = std::min(static_cast<size_t>(5), all_feature_ids.size());
            std::cout << "  演示读取前 " << demo_count << " 个要素:" << std::endl;

            for (size_t i = 0; i < demo_count; ++i) {
                uint64_t fid = all_feature_ids[i];

                try {
                    // 读取几何数据
                    auto geometry = storage_system_->readGeometry(fid);
                    if (geometry) {
                        std::cout << "    FID " << fid << ": " << geometry->getCoordinates().size() << " 个坐标点" << std::endl;
                    }

                    // 读取属性数据
                    auto attributes = storage_system_->readAttribute(fid);
                    if (attributes) {
                        std::cout << "    FID " << fid << ": " << attributes->getProperties().size() << " 个属性字段" << std::endl;
                    }

                } catch (const std::exception& e) {
                    std::cerr << "    FID " << fid << " 读取失败: " << e.what() << std::endl;
                }
            }

            // 演示S2空间查询
            if (storage_system_->isS2IndexValid()) {
                std::cout << "  S2空间索引查询演示:" << std::endl;

                // 创建一个测试查询范围（这里使用示例坐标，实际使用时需要根据数据范围调整）
                // 注意：这里需要根据实际的BBox和S2接口进行调整
                std::cout << "    S2索引状态: 有效" << std::endl;
                std::cout << "    S2单元格数: " << storage_system_->getS2IndexSize() << std::endl;
                std::cout << "    索引要素数: " << storage_system_->getS2TotalFeatureCount() << std::endl;
            } else {
                std::cout << "  S2空间索引: 无效" << std::endl;
            }

            std::cout << "  ✓ 数据访问演示完成" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 数据访问演示失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤8: 显示统计信息
    void displayStatistics() {
        std::cout << "\n步骤8: 处理统计信息" << std::endl;
        std::cout << "====================" << std::endl;

        stats_.total_time = stats_.conversion_time + stats_.index_build_time;

        std::cout << "处理时间统计:" << std::endl;
        std::cout << "  转换时间: " << std::fixed << std::setprecision(3) << stats_.conversion_time << " 秒" << std::endl;
        std::cout << "  索引构建: " << std::fixed << std::setprecision(3) << stats_.index_build_time << " 秒" << std::endl;
        std::cout << "  总时间: " << std::fixed << std::setprecision(3) << stats_.total_time << " 秒" << std::endl;

        std::cout << "\n数据统计:" << std::endl;
        std::cout << "  总要素数: " << stats_.total_features << std::endl;
        std::cout << "  有效要素数: " << stats_.valid_features << std::endl;
        std::cout << "  压缩率: " << std::fixed << std::setprecision(2) << stats_.compression_ratio << "%" << std::endl;

        std::cout << "\n文件大小统计:" << std::endl;
        std::cout << "  几何数据: " << formatFileSize(stats_.geometry_size) << std::endl;
        std::cout << "  属性数据: " << formatFileSize(stats_.attribute_size) << std::endl;
        std::cout << "  索引数据: " << formatFileSize(stats_.index_size) << std::endl;
        std::cout << "  S2索引: " << formatFileSize(stats_.s2_index_size) << std::endl;

        size_t total_size = stats_.geometry_size + stats_.attribute_size + stats_.index_size + stats_.s2_index_size;
        std::cout << "  总大小: " << formatFileSize(total_size) << std::endl;

        std::cout << "\n性能统计:" << std::endl;
        if (stats_.total_time > 0) {
            double features_per_second = stats_.valid_features / stats_.total_time;
            std::cout << "  处理速度: " << std::fixed << std::setprecision(0) << features_per_second << " 要素/秒" << std::endl;
        }

        if (stats_.geometry_size > 0) {
            double mb_per_second = (stats_.geometry_size / (1024.0 * 1024.0)) / stats_.conversion_time;
            std::cout << "  几何数据写入速度: " << std::fixed << std::setprecision(2) << mb_per_second << " MB/秒" << std::endl;
        }

        // 显示生成的文件列表
        std::cout << "\n生成的文件:" << std::endl;
        std::vector<std::pair<std::string, std::string>> files = {{"几何数据", storage_system_->getGeometryFilePath()},
                                                                  {"属性数据", storage_system_->getAttributeFilePath()},
                                                                  {"字符串池", storage_system_->getStringPoolFilePath()},
                                                                  {"索引数据", storage_system_->getIndexFilePath()},
                                                                  {"元数据", storage_system_->getMetadataFilePath()},
                                                                  {"S2索引", storage_system_->getS2IndexFilePath()}};

        for (const auto& [type, path] : files) {
            if (std::filesystem::exists(path)) {
                auto size = std::filesystem::file_size(path);
                std::cout << "  " << type << ": " << std::filesystem::path(path).filename() << " (" << formatFileSize(size) << ")" << std::endl;
            }
        }
    }

    // 辅助函数：格式化文件大小
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
};

// 主函数
int main(int argc, char* argv[]) {
    // 检查命令行参数
    if (argc != 3) {
        std::cout << "用法: " << argv[0] << " <输入Shapefile路径> <输出目录>" << std::endl;
        std::cout << "示例: " << argv[0] << " ../data/test.shp ./output" << std::endl;
        return 1;
    }

    std::string input_shapefile = argv[1];
    std::string output_dir = argv[2];

    // 创建并运行演示
    GisStorageDemo demo(input_shapefile, output_dir);

    bool success = demo.runCompleteWorkflow();

    if (success) {
        std::cout << "\n🎉 所有处理步骤都成功完成！" << std::endl;
        std::cout << "生成的文件位于: " << output_dir << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ 处理过程中遇到错误，请检查日志信息。" << std::endl;
        return 1;
    }
}
