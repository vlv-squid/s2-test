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
#include <cmath>

// GIS存储模块
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/ogr_format_converter.h"

// GIS索引模块
#include "gisindex/s2spatial_index.h"

// GDAL初始化
#include <ogrsf_frmts.h>

// 演示模式枚举
enum class DemoMode {
    FULL_PROCESSING, // 完整处理流程
    TILE_QUERY_ONLY  // 仅瓦片查询演示
};

class GisStorageDemo {
  public:
    // 完整处理流程构造函数
    GisStorageDemo(const std::string& input_shapefile, const std::string& output_dir)
        : input_shapefile_(input_shapefile)
        , output_dir_(output_dir)
        , mode_(DemoMode::FULL_PROCESSING) {
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

    // 仅瓦片查询演示构造函数
    GisStorageDemo(const std::string& existing_data_dir, const std::string& dataset_name, bool is_tile_query)
        : output_dir_(existing_data_dir)
        , mode_(DemoMode::TILE_QUERY_ONLY) {
        // 初始化GDAL
        GDALAllRegister();

        // 如果指定了数据集名称，使用指定的名称；否则从目录名提取
        if (!dataset_name.empty()) {
            dataset_name_ = dataset_name;
        } else {
            std::filesystem::path path(existing_data_dir);
            dataset_name_ = path.filename().string();
        }

        std::cout << "=== GIS存储系统瓦片查询演示 ===" << std::endl;
        std::cout << "数据目录: " << output_dir_ << std::endl;
        std::cout << "数据集名称: " << dataset_name_ << std::endl;
        std::cout << std::endl;
    }

    // 执行完整的处理流程
    bool runCompleteWorkflow() {
        try {
            if (mode_ == DemoMode::FULL_PROCESSING) {
                return runFullProcessing();
            } else if (mode_ == DemoMode::TILE_QUERY_ONLY) {
                return runTileQueryOnly();
            }
            return false;

        } catch (const std::exception& e) {
            std::cerr << "处理过程中发生错误: " << e.what() << std::endl;
            return false;
        }
    }

    // 完整处理流程
    bool runFullProcessing() {
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

        // 步骤8: 云南全省瓦片查询演示
        if (!demonstrateYunnanTileQueries()) {
            return false;
        }

        // 步骤9: 显示统计信息
        displayStatistics();

        std::cout << "\n=== 处理完成！===" << std::endl;
        return true;
    }

    // 仅瓦片查询演示流程
    bool runTileQueryOnly() {
        // 步骤1: 轻量级加载（只加载S2索引和元数据）
        if (!loadLightweightForTileQuery()) {
            return false;
        }

        // 步骤2: 属性查询演示
        std::cout << "\n步骤2: 属性查询演示..." << std::endl;
        demonstrateAttributeQueries();

        // 步骤3: 云南全省瓦片查询演示
        if (!demonstrateYunnanTileQueries()) {
            return false;
        }

        // 步骤4: 显示统计信息
        displayStatistics();

        std::cout << "\n=== 瓦片查询演示完成！===" << std::endl;
        return true;
    }

  private:
    std::string input_shapefile_;
    std::string output_dir_;
    std::string dataset_name_;
    DemoMode mode_;
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
        size_t string_pool_size = 0;
        size_t index_size = 0;
        size_t s2_index_size = 0;
        size_t total_size = 0;
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
        // auto file_size = std::filesystem::file_size(input_shapefile_);
        // std::cout << "  文件大小: " << formatFileSize(file_size) << std::endl;

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
            GisStorage::OGRFormatConverter converter(input_shapefile_, output_dir_);
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

            // 显示坐标系统信息
            if (!metadata.source_coordinate_system.empty()) {
                std::cout << "  源坐标系统: " << metadata.source_coordinate_system << std::endl;
            }
            if (!metadata.target_coordinate_system.empty()) {
                std::cout << "  目标坐标系统: " << metadata.target_coordinate_system << std::endl;
            }

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
            stats_.string_pool_size = storage_stats.string_pool_size_bytes;
            stats_.index_size = storage_stats.index_size_bytes;
            stats_.s2_index_size = storage_stats.s2_index_size_bytes;
            stats_.total_size = storage_stats.total_size_bytes;

            std::cout << "  ✓ 所有文件验证通过" << std::endl;
            std::cout << "  几何数据: " << formatFileSize(stats_.geometry_size) << std::endl;
            std::cout << "  属性数据: " << formatFileSize(stats_.attribute_size) << std::endl;
            std::cout << "  字符串池: " << formatFileSize(stats_.string_pool_size) << std::endl;
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

            // 演示属性查询
            std::cout << "  属性查询演示:" << std::endl;
            demonstrateAttributeQueries();

            std::cout << "  ✓ 数据访问演示完成" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 数据访问演示失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 轻量级加载（仅用于瓦片查询）
    bool loadLightweightForTileQuery() {
        std::cout << "\n步骤1: 轻量级加载（仅S2索引和元数据）..." << std::endl;

        try {
            // 检查数据目录是否存在
            if (!std::filesystem::exists(output_dir_)) {
                std::cerr << "错误: 数据目录不存在: " << output_dir_ << std::endl;
                return false;
            }

            // 自动检测数据集名称
            std::string detected_dataset_name = detectDatasetName();
            if (detected_dataset_name.empty()) {
                std::cerr << "错误: 无法检测到有效的数据集文件" << std::endl;
                return false;
            }

            std::cout << "  检测到数据集: " << detected_dataset_name << std::endl;

            // 初始化存储系统
            storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(output_dir_);

            // 手动设置数据集名称（轻量级模式）
            storage_system_->setDatasetNameLightweight(detected_dataset_name);

            // 获取元数据信息
            const auto& metadata = storage_system_->getMetadata();
            stats_.total_features = metadata.total_features;
            stats_.valid_features = metadata.valid_features;

            std::cout << "  ✓ 元数据加载完成" << std::endl;
            std::cout << "  总要素数: " << stats_.total_features << std::endl;
            std::cout << "  有效要素数: " << stats_.valid_features << std::endl;

            // 检查S2索引状态
            std::cout << "  ✓ S2索引加载完成" << std::endl;
            std::cout << "  S2索引文件: " << storage_system_->getS2IndexFilePath() << std::endl;
            std::cout << "  S2索引状态: " << (storage_system_->isS2IndexValid() ? "有效" : "无效") << std::endl;
            if (storage_system_->isS2IndexValid()) {
                std::cout << "  S2单元格数: " << storage_system_->getS2IndexSize() << std::endl;
                std::cout << "  索引要素数: " << storage_system_->getS2TotalFeatureCount() << std::endl;
            }

            // 获取文件大小统计（不加载实际数据）
            auto storage_stats = storage_system_->getStorageStats();
            stats_.geometry_size = storage_stats.geometry_size_bytes;
            stats_.attribute_size = storage_stats.attribute_size_bytes;
            stats_.string_pool_size = storage_stats.string_pool_size_bytes;
            stats_.index_size = storage_stats.index_size_bytes;
            stats_.s2_index_size = storage_stats.s2_index_size_bytes;
            stats_.total_size = storage_stats.total_size_bytes;

            std::cout << "  ✓ 轻量级加载完成" << std::endl;
            std::cout << "  几何数据: " << formatFileSize(stats_.geometry_size) << std::endl;
            std::cout << "  属性数据: " << formatFileSize(stats_.attribute_size) << std::endl;
            std::cout << "  字符串池: " << formatFileSize(stats_.string_pool_size) << std::endl;
            std::cout << "  索引数据: " << formatFileSize(stats_.index_size) << std::endl;
            std::cout << "  S2索引: " << formatFileSize(stats_.s2_index_size) << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 轻量级加载失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 加载现有存储系统
    bool loadExistingStorageSystem() {
        std::cout << "\n步骤1: 加载现有存储系统..." << std::endl;

        try {
            // 检查数据目录是否存在
            if (!std::filesystem::exists(output_dir_)) {
                std::cerr << "错误: 数据目录不存在: " << output_dir_ << std::endl;
                return false;
            }

            // 自动检测数据集名称
            std::string detected_dataset_name = detectDatasetName();
            if (detected_dataset_name.empty()) {
                std::cerr << "错误: 无法检测到有效的数据集文件" << std::endl;
                return false;
            }

            std::cout << "  检测到数据集: " << detected_dataset_name << std::endl;

            // 初始化存储系统
            storage_system_ = std::make_unique<GisStorage::GisStorageSystem>(output_dir_);

            // 手动设置数据集名称
            storage_system_->setDatasetName(detected_dataset_name);

            // 加载元数据
            storage_system_->loadMetadata();

            std::cout << "  ✓ 存储系统加载完成" << std::endl;
            std::cout << "  几何文件: " << storage_system_->getGeometryFilePath() << std::endl;
            std::cout << "  属性文件: " << storage_system_->getAttributeFilePath() << std::endl;
            std::cout << "  索引文件: " << storage_system_->getIndexFilePath() << std::endl;
            std::cout << "  元数据文件: " << storage_system_->getMetadataFilePath() << std::endl;
            std::cout << "  S2索引文件: " << storage_system_->getS2IndexFilePath() << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 存储系统加载失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 自动检测数据集名称
    std::string detectDatasetName() {
        std::vector<std::string> candidates;

        // 扫描目录中的所有.geom文件
        for (const auto& entry : std::filesystem::directory_iterator(output_dir_)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.length() >= 5 && filename.substr(filename.length() - 5) == ".geom") {
                    // 提取数据集名称（去掉.geom扩展名）
                    std::string dataset_name = filename.substr(0, filename.length() - 5);
                    candidates.push_back(dataset_name);
                }
            }
        }

        if (candidates.empty()) {
            return "";
        }

        // 如果有多个候选，选择第一个
        // 在实际应用中，可能需要更智能的选择逻辑
        return candidates[0];
    }

    // 验证现有数据完整性
    bool validateExistingData() {
        std::cout << "\n步骤2: 验证现有数据完整性..." << std::endl;

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

            // 获取元数据信息
            const auto& metadata = storage_system_->getMetadata();
            stats_.total_features = metadata.total_features;
            stats_.valid_features = metadata.valid_features;

            // 获取文件大小统计
            auto storage_stats = storage_system_->getStorageStats();
            stats_.geometry_size = storage_stats.geometry_size_bytes;
            stats_.attribute_size = storage_stats.attribute_size_bytes;
            stats_.string_pool_size = storage_stats.string_pool_size_bytes;
            stats_.index_size = storage_stats.index_size_bytes;
            stats_.s2_index_size = storage_stats.s2_index_size_bytes;
            stats_.total_size = storage_stats.total_size_bytes;

            std::cout << "  ✓ 所有文件验证通过" << std::endl;
            std::cout << "  总要素数: " << stats_.total_features << std::endl;
            std::cout << "  有效要素数: " << stats_.valid_features << std::endl;
            std::cout << "  几何数据: " << formatFileSize(stats_.geometry_size) << std::endl;
            std::cout << "  属性数据: " << formatFileSize(stats_.attribute_size) << std::endl;
            std::cout << "  字符串池: " << formatFileSize(stats_.string_pool_size) << std::endl;
            std::cout << "  索引数据: " << formatFileSize(stats_.index_size) << std::endl;
            std::cout << "  S2索引: " << formatFileSize(stats_.s2_index_size) << std::endl;

            // 检查S2索引状态
            if (storage_system_->isS2IndexValid()) {
                std::cout << "  S2索引状态: 有效" << std::endl;
                std::cout << "  S2单元格数: " << storage_system_->getS2IndexSize() << std::endl;
                std::cout << "  索引要素数: " << storage_system_->getS2TotalFeatureCount() << std::endl;
            } else {
                std::cout << "  S2索引状态: 无效" << std::endl;
            }

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 数据验证失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 步骤8: 云南全省瓦片查询演示
    bool demonstrateYunnanTileQueries() {
        std::cout << "\n步骤8: 云南全省瓦片查询演示..." << std::endl;

        try {
            if (!storage_system_->isS2IndexValid()) {
                std::cout << "  S2索引无效，跳过瓦片查询演示" << std::endl;
                return true;
            }

            // 云南全省边界框 (WGS84坐标系)
            // 经度范围: 97.5°E - 106.2°E
            // 纬度范围: 21.1°N - 29.2°N
            const double YUNNAN_MIN_LNG = 97.5;
            const double YUNNAN_MAX_LNG = 106.2;
            const double YUNNAN_MIN_LAT = 21.1;
            const double YUNNAN_MAX_LAT = 29.2;

            std::cout << "  云南全省边界框:" << std::endl;
            std::cout << "    经度: " << YUNNAN_MIN_LNG << "° - " << YUNNAN_MAX_LNG << "°" << std::endl;
            std::cout << "    纬度: " << YUNNAN_MIN_LAT << "° - " << YUNNAN_MAX_LAT << "°" << std::endl;

            // 测试不同范围的bbox查询（模拟不同缩放级别）
            std::vector<std::pair<std::string, std::pair<double, double>>> test_regions = {
              {"昆明市区", {102.7, 25.0}}, // 昆明市中心
              {"大理地区", {100.2, 25.6}}, // 大理
              {"西双版纳", {100.8, 22.0}}, // 西双版纳
              {"丽江地区", {100.2, 26.9}}, // 丽江
              {"曲靖地区", {103.8, 25.5}}, // 曲靖
              {"红河地区", {103.4, 23.4}}, // 红河
              {"玉溪地区", {102.5, 24.3}}, // 玉溪
              {"保山地区", {99.2, 25.1}},  // 保山
              {"昭通地区", {103.7, 27.3}}, // 昭通
              {"楚雄地区", {101.5, 25.0}}  // 楚雄
            };

            // 定义不同查询范围大小（模拟不同缩放级别）
            std::vector<std::pair<std::string, double>> zoom_levels = {
              {"省级视图", 2.0},    // 大范围查询
              {"地区级视图", 0.5},  // 中等范围查询
              {"县级视图", 0.1},    // 小范围查询
              {"乡镇级视图", 0.02}, // 很小范围查询
              {"村级视图", 0.005}   // 极小范围查询
            };

            for (const auto& [zoom_name, range] : zoom_levels) {
                std::cout << "\n  " << zoom_name << " 查询测试 (范围: ±" << range << "°):" << std::endl;

                size_t total_features = 0;
                size_t successful_queries = 0;
                auto start_time = std::chrono::high_resolution_clock::now();

                // 测试前5个地区
                size_t test_count = std::min(static_cast<size_t>(5), test_regions.size());

                for (size_t i = 0; i < test_count; ++i) {
                    const auto& [region_name, center] = test_regions[i];
                    auto [center_lng, center_lat] = center;

                    // 创建查询bbox
                    double min_lng = center_lng - range;
                    double max_lng = center_lng + range;
                    double min_lat = center_lat - range;
                    double max_lat = center_lat + range;

                    // 确保bbox在云南范围内
                    min_lng = std::max(min_lng, YUNNAN_MIN_LNG);
                    max_lng = std::min(max_lng, YUNNAN_MAX_LNG);
                    min_lat = std::max(min_lat, YUNNAN_MIN_LAT);
                    max_lat = std::min(max_lat, YUNNAN_MAX_LAT);

                    auto features = queryS2IndexForBBox(min_lng, min_lat, max_lng, max_lat);
                    total_features += features.size();
                    if (features.size() > 0) {
                        successful_queries++;
                    }

                    std::cout << "    " << region_name << " [" << std::fixed << std::setprecision(4) << min_lng << "," << min_lat << " - " << max_lng << "," << max_lat << "]: " << features.size() << " 个要素"
                              << std::endl;
                }

                auto end_time = std::chrono::high_resolution_clock::now();
                double query_time = std::chrono::duration<double>(end_time - start_time).count();

                std::cout << "    测试区域: " << test_count << " 个" << std::endl;
                std::cout << "    成功查询: " << successful_queries << " 个" << std::endl;
                std::cout << "    查询要素总数: " << total_features << std::endl;
                std::cout << "    查询时间: " << std::fixed << std::setprecision(3) << query_time << " 秒" << std::endl;
                if (test_count > 0) {
                    std::cout << "    平均每区域查询时间: " << std::fixed << std::setprecision(3) << query_time / test_count << " 秒" << std::endl;
                }
            }

            std::cout << "  ✓ 云南全省瓦片查询演示完成" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "  ✗ 瓦片查询演示失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 使用S2索引查询指定bbox的要素
    std::vector<uint64_t> queryS2IndexForBBox(double min_lng, double min_lat, double max_lng, double max_lat) {
        std::vector<uint64_t> results;

        if (!storage_system_->isS2IndexValid()) {
            return results;
        }

        try {
            // 使用存储系统的S2查询方法
            GisStorage::BBox query_bbox;
            query_bbox.min_x = min_lng; // 经度对应x坐标
            query_bbox.min_y = min_lat; // 纬度对应y坐标
            query_bbox.max_x = max_lng;
            query_bbox.max_y = max_lat;

            results = storage_system_->queryS2Index(query_bbox, 15); // 使用默认分辨率15

        } catch (const std::exception& e) {
            std::cerr << "    S2查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    // 属性查询演示
    void demonstrateAttributeQueries() {
        try {
            // 演示1: 查询dlbm字段值为0101的要素
            std::cout << "    演示1: 查询dlbm字段值为'0101'的要素" << std::endl;
            auto dlbm_results = storage_system_->queryByAttributeEfficient("dlbm", "0101");
            std::cout << "      找到 " << dlbm_results.size() << " 个匹配的要素" << std::endl;

            if (!dlbm_results.empty()) {
                std::cout << "      前5个匹配的要素ID: ";
                size_t show_count = std::min(static_cast<size_t>(5), dlbm_results.size());
                for (size_t i = 0; i < show_count; ++i) {
                    std::cout << dlbm_results[i];
                    if (i < show_count - 1)
                        std::cout << ", ";
                }
                std::cout << std::endl;

                // 显示第一个匹配要素的详细信息
                if (!dlbm_results.empty()) {
                    auto attr = storage_system_->readAttribute(dlbm_results[0]);
                    if (attr) {
                        std::cout << "      要素 " << dlbm_results[0] << " 的属性信息:" << std::endl;
                        const auto& properties = attr->getProperties();
                        size_t prop_count = 0;
                        for (const auto& [key, value] : properties) {
                            if (prop_count < 5) { // 只显示前5个属性
                                std::cout << "        " << key << ": " << value << std::endl;
                                prop_count++;
                            }
                        }
                        if (properties.size() > 5) {
                            std::cout << "        ... 还有 " << (properties.size() - 5) << " 个属性" << std::endl;
                        }
                    }
                }
            }

            // 演示1.5: 并行查询dlbm字段值为0101的要素（性能对比）
            std::cout << "    演示1.5: 并行查询dlbm字段值为'0101'的要素（性能对比）" << std::endl;
            auto dlbm_results_parallel = storage_system_->queryByAttributeParallel("dlbm", "0101");
            std::cout << "      并行查询找到 " << dlbm_results_parallel.size() << " 个匹配的要素" << std::endl;

            // 验证结果一致性
            if (dlbm_results.size() == dlbm_results_parallel.size()) {
                std::cout << "      ✓ 并行查询结果与串行查询结果一致" << std::endl;
            } else {
                std::cout << "      ⚠ 并行查询结果与串行查询结果不一致" << std::endl;
            }
            std::cout << std::endl;

            // 演示2: 查询dlbm字段包含"01"的要素（模式匹配）
            std::cout << "    演示2: 查询dlbm字段包含'01'的要素" << std::endl;
            auto pattern_results = storage_system_->queryByAttributePattern("dlbm", "01");
            std::cout << "      找到 " << pattern_results.size() << " 个匹配的要素" << std::endl;

            // 演示3: 获取dlbm字段的所有不同值
            std::cout << "    演示3: 获取dlbm字段的所有不同值" << std::endl;
            auto dlbm_values = storage_system_->queryAttributeValues("dlbm");
            std::cout << "      找到 " << dlbm_values.size() << " 个有dlbm值的要素" << std::endl;

            if (!dlbm_values.empty()) {
                // 统计不同值的数量
                std::map<std::string, int> value_counts;
                for (const auto& [fid, value] : dlbm_values) {
                    value_counts[value]++;
                }

                std::cout << "      dlbm字段的不同值统计:" << std::endl;
                size_t show_values = 0;
                for (const auto& [value, count] : value_counts) {
                    if (show_values < 10) { // 只显示前10个不同的值
                        std::cout << "        '" << value << "': " << count << " 个要素" << std::endl;
                        show_values++;
                    }
                }
                if (value_counts.size() > 10) {
                    std::cout << "        ... 还有 " << (value_counts.size() - 10) << " 个不同的值" << std::endl;
                }
            }

            // 演示4: 显示所有可用字段
            std::cout << "    演示4: 显示所有可用字段" << std::endl;

            // 尝试直接读取第一个要素的属性（FID=1）
            std::cout << "      尝试读取第一个要素 (FID: 1) 的属性..." << std::endl;

            try {
                auto attr = storage_system_->readAttribute(1);
                if (attr) {
                    const auto& properties = attr->getProperties();
                    std::cout << "      数据集包含 " << properties.size() << " 个字段:" << std::endl;

                    size_t field_count = 0;
                    for (const auto& [key, value] : properties) {
                        if (field_count < 10) { // 只显示前10个字段
                            std::cout << "        " << key << ": " << value << std::endl;
                            field_count++;
                        }
                    }
                    if (properties.size() > 10) {
                        std::cout << "        ... 还有 " << (properties.size() - 10) << " 个字段" << std::endl;
                    }

                    // 检查是否有dlbm字段
                    if (properties.find("dlbm") != properties.end()) {
                        std::cout << "      ✓ 找到dlbm字段！" << std::endl;
                    } else {
                        std::cout << "      ✗ 未找到dlbm字段" << std::endl;
                    }
                } else {
                    std::cout << "      ✗ 无法读取第一个要素的属性数据" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "      读取第一个要素属性失败: " << e.what() << std::endl;
            }

            std::cout << "    ✓ 属性查询演示完成" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "    ✗ 属性查询演示失败: " << e.what() << std::endl;
        }
    }

    // 步骤9: 显示统计信息
    void displayStatistics() {
        std::cout << "\n步骤9: 处理统计信息" << std::endl;
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

        std::cout << "  总大小: " << formatFileSize(stats_.total_size) << std::endl;

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
    if (argc < 2 || argc > 4) {
        std::cout << "用法:" << std::endl;
        std::cout << "  完整处理流程: " << argv[0] << " <输入GIS文件路径(.shp/.gdb)> <输出目录>" << std::endl;
        std::cout << "  仅瓦片查询:   " << argv[0] << " <现有数据目录> [数据集名称]" << std::endl;
        std::cout << std::endl;
        std::cout << "示例:" << std::endl;
        std::cout << "  " << argv[0] << " ../data/test.shp ./output" << std::endl;
        std::cout << "  " << argv[0] << " ../data/test.gdb ./output" << std::endl;
        std::cout << "  " << argv[0] << " ./output" << std::endl;
        std::cout << "  " << argv[0] << " ./output DLTB_2021CG" << std::endl;
        return 1;
    }

    bool success = false;

    if (argc == 3) {
        // 检查第二个参数是否是文件（完整处理流程）还是目录（轻量级模式）
        std::string first_arg = argv[1];
        std::string second_arg = argv[2];

        // 如果第一个参数是GIS数据文件（.shp或.gdb），则是完整处理流程
        bool is_gis_file = false;
        if (first_arg.length() >= 4) {
            std::string ext = first_arg.substr(first_arg.length() - 4);
            if (ext == ".shp" || ext == ".gdb") {
                is_gis_file = true;
            }
        }

        if (is_gis_file) {
            // 完整处理流程模式
            std::string input_shapefile = argv[1];
            std::string output_dir = argv[2];

            GisStorageDemo demo(input_shapefile, output_dir);
            success = demo.runCompleteWorkflow();

            if (success) {
                std::cout << "\n🎉 所有处理步骤都成功完成！" << std::endl;
                std::cout << "生成的文件位于: " << output_dir << std::endl;
            } else {
                std::cout << "\n❌ 处理过程中遇到错误，请检查日志信息。" << std::endl;
            }
        } else {
            // 轻量级模式，第二个参数是数据集名称
            std::string existing_data_dir = argv[1];
            std::string dataset_name = argv[2];

            GisStorageDemo demo(existing_data_dir, dataset_name, true);
            success = demo.runCompleteWorkflow();

            if (success) {
                std::cout << "\n🎉 瓦片查询演示成功完成！" << std::endl;
            } else {
                std::cout << "\n❌ 瓦片查询演示过程中遇到错误，请检查日志信息。" << std::endl;
            }
        }
    } else {
        // 仅瓦片查询模式（无数据集名称）
        std::string existing_data_dir = argv[1];

        GisStorageDemo demo(existing_data_dir, "", true);
        success = demo.runCompleteWorkflow();

        if (success) {
            std::cout << "\n🎉 瓦片查询演示成功完成！" << std::endl;
        } else {
            std::cout << "\n❌ 瓦片查询演示过程中遇到错误，请检查日志信息。" << std::endl;
        }
    }

    return success ? 0 : 1;
}
