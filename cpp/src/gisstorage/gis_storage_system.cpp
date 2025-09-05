//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/gis_storage_system.h"
#include "gisstorage/shapefile_converter.h"
#include "gisindex/s2spatial_index.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <openssl/md5.h>
#include <algorithm>

namespace GisStorage {

    // GisStorageSystem 实现
    GisStorageSystem::GisStorageSystem(const std::string& output_dir)
        : output_dir_(output_dir) {
        // 创建输出目录
        std::filesystem::create_directories(output_dir);

        // 初始化元数据
        metadata_.creation_date = getCurrentTimestamp();
    }

    std::unique_ptr<GeometryData> GisStorageSystem::readGeometry(uint64_t feature_id) {
        if (!geometry_storage_) {
            throw std::runtime_error("几何存储未初始化");
        }
        return geometry_storage_->readGeometry(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::readAttribute(uint64_t feature_id) {
        if (!attribute_storage_) {
            throw std::runtime_error("属性存储未初始化");
        }
        return attribute_storage_->readAttribute(feature_id);
    }

    std::vector<uint64_t> GisStorageSystem::getAllFeatureIds() {
        if (!geometry_storage_) {
            return {};
        }
        return geometry_storage_->getAllFeatureIds();
    }

    std::map<uint64_t, std::unique_ptr<GeometryData>> GisStorageSystem::readGeometries(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<GeometryData>> results;

        if (!geometry_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto geom = geometry_storage_->readGeometry(fid);
                if (geom) {
                    results[fid] = std::move(geom);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取几何数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    std::map<uint64_t, std::unique_ptr<AttributeData>> GisStorageSystem::readAttributes(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<AttributeData>> results;

        if (!attribute_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto attr = attribute_storage_->readAttribute(fid);
                if (attr) {
                    results[fid] = std::move(attr);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取属性数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    // 属性查询方法实现
    std::vector<uint64_t> GisStorageSystem::queryByAttribute(const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                initializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->loadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            // 获取所有要素ID - 如果几何存储未初始化，尝试从索引文件获取
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = geometry_storage_->getAllFeatureIds();
            } else {
                // 轻量级模式：从索引文件获取要素ID列表
                // 这里我们需要一个方法来从索引文件获取所有FID
                // 暂时使用一个简单的范围查询（假设FID从1开始连续）
                // 在实际应用中，应该从索引文件中读取所有FID
                std::cout << "      轻量级模式：使用范围查询获取要素ID" << std::endl;
                // 从元数据获取总要素数，然后生成FID列表
                const auto& metadata = getMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            std::cout << "      将查询 " << all_feature_ids.size() << " 个要素" << std::endl;

            // 批量读取优化：每次处理100000个要素（增加批量大小提升性能）
            const size_t batch_size = 100000;
            size_t processed = 0;
            auto start_time = std::chrono::high_resolution_clock::now();

            for (size_t i = 0; i < all_feature_ids.size(); i += batch_size) {
                size_t end_idx = std::min(i + batch_size, all_feature_ids.size());
                std::vector<uint64_t> batch_ids(all_feature_ids.begin() + i, all_feature_ids.begin() + end_idx);

                // 批量读取属性
                auto batch_attrs = readAttributes(batch_ids);

                // 处理批量结果
                for (const auto& [fid, attr] : batch_attrs) {
                    if (attr) {
                        std::string value = attr->getProperty(field_name);
                        if (value == field_value) {
                            results.push_back(fid);
                        }
                    }
                }

                processed += batch_ids.size();

                // 每处理10万个要素显示一次进度
                if (processed % 100000 == 0) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    double elapsed = std::chrono::duration<double>(current_time - start_time).count();
                    double rate = processed / elapsed;
                    std::cout << "      进度: " << processed << "/" << all_feature_ids.size() << " (" << (100.0 * processed / all_feature_ids.size()) << "%) " << "速度: " << std::fixed << std::setprecision(0) << rate
                              << " 要素/秒" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "属性查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::queryByAttributePattern(const std::string& field_name, const std::string& pattern) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                initializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->loadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            // 获取所有要素ID - 如果几何存储未初始化，尝试从索引文件获取
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = geometry_storage_->getAllFeatureIds();
            } else {
                // 轻量级模式：从元数据获取总要素数，生成FID列表
                const auto& metadata = getMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            // 遍历所有要素，查找匹配的属性模式
            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->readAttribute(fid);
                    if (attr) {
                        std::string value = attr->getProperty(field_name);
                        // 简单的包含匹配（可以扩展为正则表达式）
                        if (value.find(pattern) != std::string::npos) {
                            results.push_back(fid);
                        }
                    }
                } catch (const std::exception& e) {
                    // 忽略单个要素读取错误，继续处理其他要素
                    continue;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "属性模式查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<std::pair<uint64_t, std::string>> GisStorageSystem::queryAttributeValues(const std::string& field_name) {
        std::vector<std::pair<uint64_t, std::string>> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                initializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->loadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            // 获取所有要素ID - 如果几何存储未初始化，尝试从索引文件获取
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = geometry_storage_->getAllFeatureIds();
            } else {
                // 轻量级模式：从元数据获取总要素数，生成FID列表
                const auto& metadata = getMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            // 遍历所有要素，获取指定字段的值
            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->readAttribute(fid);
                    if (attr) {
                        std::string value = attr->getProperty(field_name);
                        if (!value.empty()) {
                            results.emplace_back(fid, value);
                        }
                    }
                } catch (const std::exception& e) {
                    // 忽略单个要素读取错误，继续处理其他要素
                    continue;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "属性值查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    void GisStorageSystem::initializeStorageFiles(const std::string& shapefile_path) {
        // 提取Shapefile名称
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        // 初始化文件路径
        initializeFilePaths();

        // 初始化存储对象
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);

        // 创建元数据
        createMetadataFromShapefile(shapefile_path);

        // 加载索引
        if (std::filesystem::exists(index_file_)) {
            geometry_storage_->loadIndexFromFile(index_file_);
            attribute_storage_->loadIndexFromFile(index_file_);
        }

        // 加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            loadMetadata();
        }

        // 更新文件统计信息
        updateFileSizes();
        updateChecksums();
    }

    void GisStorageSystem::setDatasetName(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;

        // 重新初始化文件路径
        initializeFilePaths();

        // 重新初始化存储对象
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);

        // 加载索引
        if (std::filesystem::exists(index_file_)) {
            geometry_storage_->loadIndexFromFile(index_file_);
            attribute_storage_->loadIndexFromFile(index_file_);
        }

        // 加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            loadMetadata();
        }

        // 加载S2索引
        if (std::filesystem::exists(s2_index_file_)) {
            initializeS2Index(15); // 使用默认分辨率15
            s2_spatial_index_->load();
        }

        // 更新文件统计信息
        updateFileSizes();
        updateChecksums();
    }

    void GisStorageSystem::setDatasetNameLightweight(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;

        // 重新初始化文件路径
        initializeFilePaths();

        // 轻量级模式：不初始化几何和属性存储对象
        // 这些对象将在需要时按需创建

        // 只加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            loadMetadata();
        }

        // 只加载S2索引
        if (std::filesystem::exists(s2_index_file_)) {
            initializeS2Index(15); // 使用默认分辨率15
            s2_spatial_index_->load();
        }

        // 更新文件统计信息
        updateFileSizes();
        updateChecksums();
    }

    void GisStorageSystem::initializeFilePaths() {
        geom_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::GEOMETRY_DATA;
        attr_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::ATTRIBUTE_DATA;
        pool_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::STRING_POOL;
        index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::INDEX_DATA;
        metadata_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::METADATA;
        s2_index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::S2_INDEX;
    }

    void GisStorageSystem::createMetadataFromShapefile(const std::string& shapefile_path) {
        metadata_.source_file = std::filesystem::path(shapefile_path).filename().string();
        metadata_.source_format = "Shapefile";
        metadata_.creation_date = getCurrentTimestamp();

        // 这里可以从Shapefile中提取更多信息
        // 如坐标系统、投影信息等
    }

    // S2索引管理方法
    void GisStorageSystem::initializeS2Index(int resolution) {
        if (!s2_spatial_index_) {
            s2_spatial_index_ = std::make_unique<S2Main::S2SpatialIndex>(s2_index_file_, resolution);
        }
    }

    bool GisStorageSystem::buildS2IndexFromDataset(const std::string& dataset_path, int max_features_per_cell) {
        if (!s2_spatial_index_) {
            initializeS2Index();
        }

        bool success = s2_spatial_index_->buildFromDataset(dataset_path, max_features_per_cell);
        if (success) {
            metadata_.s2_index_info = "S2索引构建成功";
            updateMetadataStats();
        }
        return success;
    }

    std::vector<uint64_t> GisStorageSystem::queryS2Index(const BBox& query_bbox, int resolution) {
        if (!s2_spatial_index_ || !isS2IndexValid()) {
            return {};
        }

        try {
            // 将BBox转换为S2LatLngRect进行查询
            S2LatLng p1 = S2LatLng::FromDegrees(query_bbox.min_y, query_bbox.min_x); // 纬度, 经度
            S2LatLng p2 = S2LatLng::FromDegrees(query_bbox.max_y, query_bbox.max_x);
            S2LatLngRect rect(p1, p2);

            // 使用S2索引进行查询
            auto s2_results = s2_spatial_index_->query(rect, resolution);

            // 将int类型的FID转换为uint64_t
            std::vector<uint64_t> results;
            results.reserve(s2_results.size());
            for (int fid : s2_results) {
                results.push_back(static_cast<uint64_t>(fid));
            }

            return results;

        } catch (const std::exception& e) {
            std::cerr << "S2查询错误: " << e.what() << std::endl;
            return {};
        }
    }

    void GisStorageSystem::saveS2Index() {
        if (s2_spatial_index_) {
            s2_spatial_index_->save();
            updateFileSizes();
            updateChecksums();
        }
    }

    void GisStorageSystem::loadS2Index() {
        if (std::filesystem::exists(s2_index_file_)) {
            initializeS2Index();
            // 这里需要实现S2索引的加载逻辑
        }
    }

    bool GisStorageSystem::isS2IndexValid() const {
        return s2_spatial_index_ && s2_spatial_index_->isIndexValid();
    }

    size_t GisStorageSystem::getS2IndexSize() const {
        return s2_spatial_index_ ? s2_spatial_index_->getIndexSize() : 0;
    }

    size_t GisStorageSystem::getS2TotalFeatureCount() const {
        return s2_spatial_index_ ? s2_spatial_index_->getTotalFeatureCount() : 0;
    }

    // 元数据管理方法
    void GisStorageSystem::updateMetadata(const Metadata& metadata) {
        metadata_ = metadata;
        saveMetadata();
    }

    void GisStorageSystem::saveMetadata() {
        nlohmann::json metadata_json;

        // 序列化元数据
        metadata_json["format_version"] = metadata_.format_version;
        metadata_json["source_format"] = metadata_.source_format;
        metadata_json["source_file"] = metadata_.source_file;
        metadata_json["creation_date"] = metadata_.creation_date;
        metadata_json["coordinate_system"] = metadata_.coordinate_system;
        metadata_json["projection_info"] = metadata_.projection_info;
        metadata_json["total_features"] = metadata_.total_features;
        metadata_json["valid_features"] = metadata_.valid_features;
        metadata_json["field_definitions"] = metadata_.field_definitions;
        metadata_json["geometry_types"] = metadata_.geometry_types;
        metadata_json["file_sizes"] = metadata_.file_sizes;
        metadata_json["checksums"] = metadata_.checksums;
        metadata_json["compression_info"] = metadata_.compression_info;
        metadata_json["index_info"] = metadata_.index_info;
        metadata_json["s2_index_info"] = metadata_.s2_index_info;

        // 保存到文件
        std::ofstream file(metadata_file_);
        if (file.is_open()) {
            file << metadata_json.dump(4);
            file.close();
        }
    }

    void GisStorageSystem::loadMetadata() {
        if (!std::filesystem::exists(metadata_file_)) {
            return;
        }

        std::ifstream file(metadata_file_);
        if (!file.is_open()) {
            return;
        }

        try {
            nlohmann::json metadata_json;
            file >> metadata_json;

            // 反序列化元数据
            metadata_.format_version = metadata_json.value("format_version", "1.0");
            metadata_.source_format = metadata_json.value("source_format", "Shapefile");
            metadata_.source_file = metadata_json.value("source_file", "");
            metadata_.creation_date = metadata_json.value("creation_date", "");
            metadata_.coordinate_system = metadata_json.value("coordinate_system", "");
            metadata_.projection_info = metadata_json.value("projection_info", "");
            metadata_.total_features = metadata_json.value("total_features", 0);
            metadata_.valid_features = metadata_json.value("valid_features", 0);
            metadata_.field_definitions = metadata_json.value("field_definitions", std::map<std::string, std::string>{});
            metadata_.geometry_types = metadata_json.value("geometry_types", std::map<std::string, std::string>{});
            metadata_.file_sizes = metadata_json.value("file_sizes", std::map<std::string, size_t>{});
            metadata_.checksums = metadata_json.value("checksums", std::map<std::string, std::string>{});
            metadata_.compression_info = metadata_json.value("compression_info", "");
            metadata_.index_info = metadata_json.value("index_info", "");
            metadata_.s2_index_info = metadata_json.value("s2_index_info", "");
        } catch (const std::exception& e) {
            std::cerr << "加载元数据失败: " << e.what() << std::endl;
        }
    }

    void GisStorageSystem::updateFileSizes() {
        metadata_.file_sizes.clear();

        if (std::filesystem::exists(geom_file_)) {
            metadata_.file_sizes["geometry"] = std::filesystem::file_size(geom_file_);
        }
        if (std::filesystem::exists(attr_file_)) {
            metadata_.file_sizes["attribute"] = std::filesystem::file_size(attr_file_);
        }
        if (std::filesystem::exists(pool_file_)) {
            metadata_.file_sizes["string_pool"] = std::filesystem::file_size(pool_file_);
        }
        if (std::filesystem::exists(index_file_)) {
            metadata_.file_sizes["index"] = std::filesystem::file_size(index_file_);
        }
        if (std::filesystem::exists(s2_index_file_)) {
            metadata_.file_sizes["s2_index"] = std::filesystem::file_size(s2_index_file_);
        }
    }

    void GisStorageSystem::updateChecksums() {
        metadata_.checksums.clear();

        if (std::filesystem::exists(geom_file_)) {
            metadata_.checksums["geometry"] = calculateFileChecksum(geom_file_);
        }
        if (std::filesystem::exists(attr_file_)) {
            metadata_.checksums["attribute"] = calculateFileChecksum(attr_file_);
        }
        if (std::filesystem::exists(pool_file_)) {
            metadata_.checksums["string_pool"] = calculateFileChecksum(pool_file_);
        }
        if (std::filesystem::exists(index_file_)) {
            metadata_.checksums["index"] = calculateFileChecksum(index_file_);
        }
        if (std::filesystem::exists(s2_index_file_)) {
            metadata_.checksums["s2_index"] = calculateFileChecksum(s2_index_file_);
        }
    }

    void GisStorageSystem::updateMetadataStats() {
        if (geometry_storage_) {
            metadata_.total_features = geometry_storage_->getAllFeatureIds().size();
            metadata_.valid_features = metadata_.total_features;
        }

        if (s2_spatial_index_) {
            metadata_.s2_index_info = "S2索引大小: " + std::to_string(getS2IndexSize()) + ", 要素数量: " + std::to_string(getS2TotalFeatureCount());
        }
    }

    // 文件路径获取方法
    std::string GisStorageSystem::getGeometryFilePath() const {
        return geom_file_;
    }
    std::string GisStorageSystem::getAttributeFilePath() const {
        return attr_file_;
    }
    std::string GisStorageSystem::getStringPoolFilePath() const {
        return pool_file_;
    }
    std::string GisStorageSystem::getIndexFilePath() const {
        return index_file_;
    }
    std::string GisStorageSystem::getMetadataFilePath() const {
        return metadata_file_;
    }
    std::string GisStorageSystem::getS2IndexFilePath() const {
        return s2_index_file_;
    }

    // 存储统计信息
    GisStorageSystem::StorageStats GisStorageSystem::getStorageStats() const {
        StorageStats stats;

        // 只统计5个核心文件，不包含元数据文件
        std::vector<std::string> core_files = {"geometry", "attribute", "string_pool", "index", "s2_index"};

        for (const auto& file_type : core_files) {
            auto it = metadata_.file_sizes.find(file_type);
            if (it != metadata_.file_sizes.end()) {
                stats.total_files++;
                stats.total_size_bytes += it->second;

                if (file_type == "geometry") {
                    stats.geometry_size_bytes = it->second;
                } else if (file_type == "attribute") {
                    stats.attribute_size_bytes = it->second;
                } else if (file_type == "string_pool") {
                    stats.string_pool_size_bytes = it->second;
                } else if (file_type == "index") {
                    stats.index_size_bytes = it->second;
                } else if (file_type == "s2_index") {
                    stats.s2_index_size_bytes = it->second;
                }
            }
        }

        return stats;
    }

    // 私有辅助方法
    std::string GisStorageSystem::calculateFileChecksum(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }

        // 简单的MD5计算（这里使用简化版本）
        std::stringstream ss;
        ss << std::hex << std::hash<std::string>{}(file_path + std::to_string(std::filesystem::file_size(file_path)));
        return ss.str();
    }

    std::string GisStorageSystem::getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

} // namespace GisStorage
