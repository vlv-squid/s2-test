//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/gis_storage_system.h"
#include "gisstorage/ogr_format_converter.h"
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
#include <cmath>
#include <tbb/tbb.h>
#include <mutex>

namespace GisStorage {

    // GisStorageSystem 实现
    GisStorageSystem::GisStorageSystem(const std::string& output_dir)
        : output_dir_(output_dir) {
        // 创建输出目录
        std::filesystem::create_directories(output_dir);

        // 初始化元数据
        metadata_.creation_date = GetCurrentTimestamp();
    }

    std::unique_ptr<GeometryData> GisStorageSystem::ReadGeometry(uint64_t feature_id) {
        if (!geometry_storage_) {
            throw std::runtime_error("几何存储未初始化");
        }
        return geometry_storage_->ReadGeometry(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::ReadAttribute(uint64_t feature_id) {
        if (!attribute_storage_) {
            throw std::runtime_error("属性存储未初始化");
        }
        return attribute_storage_->ReadAttribute(feature_id);
    }

    std::vector<uint64_t> GisStorageSystem::GetAllFeatureIds() {
        if (!geometry_storage_) {
            return {};
        }
        return geometry_storage_->GetAllFeatureIds();
    }

    std::map<uint64_t, std::unique_ptr<GeometryData>> GisStorageSystem::ReadGeometries(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<GeometryData>> results;

        if (!geometry_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto geom = geometry_storage_->ReadGeometry(fid);
                if (geom) {
                    results[fid] = std::move(geom);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取几何数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    std::map<uint64_t, std::unique_ptr<AttributeData>> GisStorageSystem::ReadAttributes(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<AttributeData>> results;

        if (!attribute_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto attr = attribute_storage_->ReadAttribute(fid);
                if (attr) {
                    results[fid] = std::move(attr);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取属性数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QueryByAttributeEfficient(const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                InitializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->LoadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      基于字符串池的高效属性查询：字段='" << field_name << "', 值='" << field_value << "'" << std::endl;

            auto start_time = std::chrono::high_resolution_clock::now();

            // 获取字符串池的压缩统计信息
            auto stats = attribute_storage_->GetCompressionStats();
            std::cout << "      字符串池包含 " << stats.unique_strings << " 个唯一字符串" << std::endl;

            // 注意：当前的实现仍然需要遍历属性数据
            // 真正的优化需要：
            // 1. 构建字段值到要素ID的反向索引
            // 2. 或者修改属性存储格式以支持快速查询

            // 当前实现：使用批量读取优化
            const auto& metadata = GetMetadata();
            size_t total_features = metadata.total_features;

            std::cout << "      将查询 " << total_features << " 个要素（批量读取优化）" << std::endl;

            // 批量读取优化：每次处理100000个要素
            const size_t batch_size = 100000;
            size_t processed = 0;
            size_t found_count = 0;

            for (size_t i = 1; i <= total_features; i += batch_size) {
                size_t end_fid = std::min(i + batch_size - 1, total_features);
                std::vector<uint64_t> batch_ids;
                batch_ids.reserve(batch_size);

                for (uint64_t fid = i; fid <= end_fid; ++fid) {
                    batch_ids.push_back(fid);
                }

                // 批量读取属性
                auto batch_attrs = ReadAttributes(batch_ids);

                // 处理批量结果
                for (const auto& [fid, attr] : batch_attrs) {
                    if (attr) {
                        std::string value = attr->GetProperty(field_name);
                        if (value == field_value) {
                            results.push_back(fid);
                            found_count++;
                        }
                    }
                }

                processed += batch_ids.size();

                // 每处理10万个要素显示一次进度
                if (processed % 100000 == 0) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    double speed = processed / (elapsed.count() / 1000.0);
                    std::cout << "\r      进度: " << processed << "/" << total_features << " (" << (100.0 * processed / total_features) << "%) " << "找到: " << found_count << " 速度: " << static_cast<int>(speed)
                              << " 要素/秒" << std::flush;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      属性查询完成，找到 " << results.size() << " 个匹配要素，耗时 " << total_elapsed.count() << " ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "属性查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QueryByAttributeParallel(const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                InitializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->LoadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      并行属性查询：字段='" << field_name << "', 值='" << field_value << "'" << std::endl;

            auto start_time = std::chrono::high_resolution_clock::now();

            // 获取字符串池的压缩统计信息
            auto stats = attribute_storage_->GetCompressionStats();
            std::cout << "      字符串池包含 " << stats.unique_strings << " 个唯一字符串" << std::endl;

            // 获取总要素数
            const auto& metadata = GetMetadata();
            size_t total_features = metadata.total_features;

            std::cout << "      将并行查询 " << total_features << " 个要素" << std::endl;

            // 使用TBB并行处理
            tbb::concurrent_vector<uint64_t> concurrent_results;
            std::atomic<size_t> processed_count{0};
            std::atomic<size_t> found_count{0};
            std::mutex progress_mutex; // 用于进度输出的线程安全

            // 并行处理要素
            tbb::parallel_for(tbb::blocked_range<size_t>(1, total_features + 1), [&](const tbb::blocked_range<size_t>& range) {
                std::vector<uint64_t> local_results; // 本地结果缓存，减少锁竞争
                local_results.reserve(1000);         // 预分配空间

                for (size_t fid = range.begin(); fid != range.end(); ++fid) {
                    try {
                        auto attr = attribute_storage_->ReadAttribute(fid);
                        if (attr) {
                            std::string value = attr->GetProperty(field_name);
                            if (value == field_value) {
                                local_results.push_back(fid);
                            }
                        }
                    } catch (const std::exception& e) {
                        // 忽略单个要素读取错误，继续处理其他要素
                        continue;
                    }

                    size_t current_processed = processed_count.fetch_add(1) + 1;

                    // 每处理10万个要素显示一次进度（使用锁确保输出不混乱）
                    if (current_processed % 100000 == 0) {
                        std::lock_guard<std::mutex> lock(progress_mutex);
                        auto current_time = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                        double speed = current_processed / (elapsed.count() / 1000.0);
                        std::cout << "\r      进度: " << current_processed << "/" << total_features << " (" << (100.0 * current_processed / total_features) << "%) " << "找到: " << found_count.load()
                                  << " 速度: " << static_cast<int>(speed) << " 要素/秒" << std::flush;
                    }
                }

                // 批量添加本地结果到并发向量
                if (!local_results.empty()) {
                    for (const auto& fid : local_results) {
                        concurrent_results.push_back(fid);
                    }
                    found_count.fetch_add(local_results.size());
                }
            });

            // 将并发结果转换为普通向量
            results.reserve(concurrent_results.size());
            for (const auto& fid : concurrent_results) {
                results.push_back(fid);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      并行属性查询完成，找到 " << results.size() << " 个匹配要素，耗时 " << total_elapsed.count() << " ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "并行属性查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QuerySpatialAttributeEfficient(const BBox& spatial_bbox, const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                InitializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->LoadIndexFromFile(index_file_);
                }
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      高效复合查询：空间范围=[" << spatial_bbox.min_x << "," << spatial_bbox.min_y << " - " << spatial_bbox.max_x << "," << spatial_bbox.max_y << "], 字段='" << field_name << "', 值='"
                      << field_value << "'" << std::endl;

            auto start_time = std::chrono::high_resolution_clock::now();

            // 第一步：空间查询获取候选要素ID
            auto spatial_results = QueryS2Index(spatial_bbox);
            std::cout << "      空间查询找到 " << spatial_results.size() << " 个候选要素" << std::endl;

            if (spatial_results.empty()) {
                std::cout << "      空间查询无结果，复合查询完成" << std::endl;
                return results;
            }

            // 第二步：对空间查询结果进行批量属性查询
            const size_t batch_size = 10000; // 批量处理空间查询结果
            size_t processed = 0;
            size_t found_count = 0;

            for (size_t i = 0; i < spatial_results.size(); i += batch_size) {
                size_t end_idx = std::min(i + batch_size, spatial_results.size());
                std::vector<uint64_t> batch_ids(spatial_results.begin() + i, spatial_results.begin() + end_idx);

                // 批量读取属性
                auto batch_attrs = ReadAttributes(batch_ids);

                // 处理批量结果
                for (const auto& [fid, attr] : batch_attrs) {
                    if (attr) {
                        std::string value = attr->GetProperty(field_name);
                        if (value == field_value) {
                            results.push_back(fid);
                            found_count++;
                        }
                    }
                }

                processed += batch_ids.size();

                // 每处理1万个要素显示一次进度
                if (processed % 50000 == 0 || processed == spatial_results.size()) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    double speed = processed / (elapsed.count() / 1000.0);
                    std::cout << "\r      属性过滤进度: " << processed << "/" << spatial_results.size() << " (" << (100.0 * processed / spatial_results.size()) << "%) " << "找到: " << found_count
                              << " 速度: " << static_cast<int>(speed) << " 要素/秒" << std::flush;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      复合查询完成，从 " << spatial_results.size() << " 个空间候选要素中找到 " << results.size() << " 个匹配要素，耗时 " << total_elapsed.count() << " ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "复合查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QueryByAttributePattern(const std::string& field_name, const std::string& pattern) {
        std::vector<uint64_t> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                InitializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->LoadIndexFromFile(index_file_);
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
                all_feature_ids = geometry_storage_->GetAllFeatureIds();
            } else {
                // 轻量级模式：从元数据获取总要素数，生成FID列表
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            // 遍历所有要素，查找匹配的属性模式
            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->ReadAttribute(fid);
                    if (attr) {
                        std::string value = attr->GetProperty(field_name);
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

    std::vector<std::pair<uint64_t, std::string>> GisStorageSystem::QueryAttributeValues(const std::string& field_name) {
        std::vector<std::pair<uint64_t, std::string>> results;

        // 如果属性存储未初始化，尝试按需初始化
        if (!attribute_storage_) {
            try {
                // 重新初始化文件路径
                InitializeFilePaths();
                // 初始化属性存储对象
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                // 加载索引
                if (std::filesystem::exists(index_file_)) {
                    attribute_storage_->LoadIndexFromFile(index_file_);
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
                all_feature_ids = geometry_storage_->GetAllFeatureIds();
            } else {
                // 轻量级模式：从元数据获取总要素数，生成FID列表
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            // 遍历所有要素，获取指定字段的值
            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->ReadAttribute(fid);
                    if (attr) {
                        std::string value = attr->GetProperty(field_name);
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

    void GisStorageSystem::InitializeStorageFiles(const std::string& shapefile_path) {
        // 提取Shapefile名称
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        // 初始化文件路径
        InitializeFilePaths();

        // 初始化存储对象
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);

        // 创建元数据
        CreateMetadataFromShapefile(shapefile_path);

        // 加载索引
        if (std::filesystem::exists(index_file_)) {
            geometry_storage_->LoadIndexFromFile(index_file_);
            attribute_storage_->LoadIndexFromFile(index_file_);
        }

        // 加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }

        // 更新文件统计信息
        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::SetDatasetName(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;

        // 重新初始化文件路径
        InitializeFilePaths();

        // 重新初始化存储对象
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);

        // 加载索引
        if (std::filesystem::exists(index_file_)) {
            geometry_storage_->LoadIndexFromFile(index_file_);
            attribute_storage_->LoadIndexFromFile(index_file_);
        }

        // 加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }

        // 加载S2索引
        if (std::filesystem::exists(s2_index_file_)) {
            InitializeS2Index(15); // 使用默认分辨率15
            s2_spatial_index_->Load();
        }

        // 更新文件统计信息
        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::SetDatasetNameLightweight(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;

        // 重新初始化文件路径
        InitializeFilePaths();

        // 轻量级模式：不初始化几何和属性存储对象
        // 这些对象将在需要时按需创建

        // 只加载元数据
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }

        // 只加载S2索引
        if (std::filesystem::exists(s2_index_file_)) {
            InitializeS2Index(15); // 使用默认分辨率15
            s2_spatial_index_->Load();
        }

        // 更新文件统计信息
        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::InitializeFilePaths() {
        geom_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::GEOMETRY_DATA;
        attr_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::ATTRIBUTE_DATA;
        pool_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::STRING_POOL;
        index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::INDEX_DATA;
        metadata_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::METADATA;
        s2_index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::S2_INDEX;
    }

    void GisStorageSystem::CreateMetadataFromShapefile(const std::string& shapefile_path) {
        metadata_.source_file = std::filesystem::path(shapefile_path).filename().string();
        metadata_.source_format = "Shapefile";
        metadata_.creation_date = GetCurrentTimestamp();

        // 这里可以从Shapefile中提取更多信息
        // 如坐标系统、投影信息等
    }

    // S2索引管理方法
    void GisStorageSystem::InitializeS2Index(int resolution) {
        if (!s2_spatial_index_) {
            s2_spatial_index_ = std::make_unique<S2Main::S2SpatialIndex>(s2_index_file_, resolution);
        }
    }

    bool GisStorageSystem::BuildS2IndexFromDataset(const std::string& dataset_path, int max_features_per_cell) {
        if (!s2_spatial_index_) {
            InitializeS2Index();
        }

        bool success = s2_spatial_index_->BuildFromDataset(dataset_path, max_features_per_cell);
        if (success) {
            metadata_.s2_index_info = "S2索引构建成功";
            UpdateMetadataStats();
        }
        return success;
    }

    std::vector<uint64_t> GisStorageSystem::QueryS2Index(const BBox& query_bbox, int resolution) {
        if (!s2_spatial_index_ || !IsS2IndexValid()) {
            return {};
        }

        try {
            // 将BBox转换为S2LatLngRect进行查询
            S2LatLng p1 = S2LatLng::FromDegrees(query_bbox.min_y, query_bbox.min_x); // 纬度, 经度
            S2LatLng p2 = S2LatLng::FromDegrees(query_bbox.max_y, query_bbox.max_x);
            S2LatLngRect rect(p1, p2);

            // 使用S2索引进行查询
            auto s2_results = s2_spatial_index_->Query(rect, resolution);

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

    void GisStorageSystem::SaveS2Index() {
        if (s2_spatial_index_) {
            s2_spatial_index_->Save();
            UpdateFileSizes();
            UpdateChecksums();
        }
    }

    void GisStorageSystem::LoadS2Index() {
        if (std::filesystem::exists(s2_index_file_)) {
            InitializeS2Index();
            // 这里需要实现S2索引的加载逻辑
        }
    }

    bool GisStorageSystem::IsS2IndexValid() const {
        return s2_spatial_index_ && s2_spatial_index_->IsIndexValid();
    }

    size_t GisStorageSystem::GetS2IndexSize() const {
        return s2_spatial_index_ ? s2_spatial_index_->GetIndexSize() : 0;
    }

    size_t GisStorageSystem::GetS2TotalFeatureCount() const {
        return s2_spatial_index_ ? s2_spatial_index_->GetTotalFeatureCount() : 0;
    }

    // 元数据管理方法
    void GisStorageSystem::UpdateMetadata(const Metadata& metadata) {
        metadata_ = metadata;
        SaveMetadata();
    }

    void GisStorageSystem::SaveMetadata() {
        nlohmann::json metadata_json;

        // 首先尝试读取现有的元数据文件，保留字段定义和空间范围
        if (std::filesystem::exists(metadata_file_)) {
            std::ifstream file(metadata_file_);
            if (file.is_open()) {
                try {
                    file >> metadata_json;
                } catch (const std::exception& e) {
                    std::cerr << "读取现有元数据失败: " << e.what() << std::endl;
                }
            }
        }

        // 更新元数据字段
        metadata_json["format_version"] = metadata_.format_version;
        metadata_json["source_format"] = metadata_.source_format;
        metadata_json["source_file"] = metadata_.source_file;
        metadata_json["creation_date"] = metadata_.creation_date;
        metadata_json["source_coordinate_system"] = metadata_.source_coordinate_system;
        metadata_json["target_coordinate_system"] = metadata_.target_coordinate_system;
        metadata_json["total_features"] = metadata_.total_features;
        metadata_json["valid_features"] = metadata_.valid_features;
        metadata_json["file_sizes"] = metadata_.file_sizes;
        metadata_json["checksums"] = metadata_.checksums;
        // 处理compression_info，如果它是JSON字符串则解析为对象
        // 如果现有元数据中已经有compression_info，则保留它
        if (!metadata_json.contains("compression_info") || metadata_json["compression_info"].empty()) {
            if (!metadata_.compression_info.empty()) {
                try {
                    nlohmann::json compression_json = nlohmann::json::parse(metadata_.compression_info);
                    metadata_json["compression_info"] = compression_json;
                } catch (const std::exception&) {
                    // 如果不是有效的JSON，则作为字符串保存
                    metadata_json["compression_info"] = metadata_.compression_info;
                }
            } else {
                metadata_json["compression_info"] = "";
            }
        }
        metadata_json["s2_index_info"] = metadata_.s2_index_info;

        // 只有在字段定义和空间范围不存在时才使用默认值
        if (!metadata_json.contains("field_definitions") || metadata_json["field_definitions"].empty()) {
            metadata_json["field_definitions"] = metadata_.field_definitions;
        }

        // 只有在空间范围不存在或为无效值时才使用默认值
        if (!metadata_json.contains("spatial_extent") ||
            (metadata_json["spatial_extent"]["min_x"] == 0.0 && metadata_json["spatial_extent"]["min_y"] == 0.0 && metadata_json["spatial_extent"]["max_x"] == 0.0 && metadata_json["spatial_extent"]["max_y"] == 0.0) ||
            (std::abs(static_cast<double>(metadata_json["spatial_extent"]["min_x"])) > 1e9 || std::abs(static_cast<double>(metadata_json["spatial_extent"]["min_y"])) > 1e9 ||
             std::abs(static_cast<double>(metadata_json["spatial_extent"]["max_x"])) > 1e9 || std::abs(static_cast<double>(metadata_json["spatial_extent"]["max_y"])) > 1e9)) {
            // 只有在metadata_中的空间范围也是无效值时才使用默认值
            if (std::abs(metadata_.spatial_extent.min_x) > 1e9 || std::abs(metadata_.spatial_extent.min_y) > 1e9 || std::abs(metadata_.spatial_extent.max_x) > 1e9 || std::abs(metadata_.spatial_extent.max_y) > 1e9) {
                nlohmann::json spatial_extent_json;
                spatial_extent_json["min_x"] = metadata_.spatial_extent.min_x;
                spatial_extent_json["min_y"] = metadata_.spatial_extent.min_y;
                spatial_extent_json["max_x"] = metadata_.spatial_extent.max_x;
                spatial_extent_json["max_y"] = metadata_.spatial_extent.max_y;
                metadata_json["spatial_extent"] = spatial_extent_json;
            }
        }

        // 保存到文件
        std::ofstream file(metadata_file_);
        if (file.is_open()) {
            file << metadata_json.dump(4);
            file.close();
        }
    }

    void GisStorageSystem::LoadMetadata() {
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
            metadata_.source_coordinate_system = metadata_json.value("source_coordinate_system", "");
            metadata_.target_coordinate_system = metadata_json.value("target_coordinate_system", "");
            metadata_.total_features = metadata_json.value("total_features", 0);
            metadata_.valid_features = metadata_json.value("valid_features", 0);
            metadata_.field_definitions = metadata_json.value("field_definitions", std::map<std::string, std::string>{});
            metadata_.file_sizes = metadata_json.value("file_sizes", std::map<std::string, size_t>{});
            metadata_.checksums = metadata_json.value("checksums", std::map<std::string, std::string>{});
            // 处理compression_info，它可能是一个JSON对象或字符串
            if (metadata_json.contains("compression_info")) {
                if (metadata_json["compression_info"].is_string()) {
                    metadata_.compression_info = metadata_json["compression_info"];
                } else {
                    metadata_.compression_info = metadata_json["compression_info"].dump();
                }
            } else {
                metadata_.compression_info = "";
            }
            metadata_.s2_index_info = metadata_json.value("s2_index_info", "");

            // 加载空间范围
            if (metadata_json.contains("spatial_extent")) {
                auto spatial_extent_json = metadata_json["spatial_extent"];
                metadata_.spatial_extent.min_x = spatial_extent_json.value("min_x", 0.0);
                metadata_.spatial_extent.min_y = spatial_extent_json.value("min_y", 0.0);
                metadata_.spatial_extent.max_x = spatial_extent_json.value("max_x", 0.0);
                metadata_.spatial_extent.max_y = spatial_extent_json.value("max_y", 0.0);
            }
        } catch (const std::exception& e) {
            std::cerr << "加载元数据失败: " << e.what() << std::endl;
        }
    }

    void GisStorageSystem::UpdateFileSizes() {
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

    void GisStorageSystem::UpdateChecksums() {
        metadata_.checksums.clear();

        if (std::filesystem::exists(geom_file_)) {
            metadata_.checksums["geometry"] = CalculateFileChecksum(geom_file_);
        }
        if (std::filesystem::exists(attr_file_)) {
            metadata_.checksums["attribute"] = CalculateFileChecksum(attr_file_);
        }
        if (std::filesystem::exists(pool_file_)) {
            metadata_.checksums["string_pool"] = CalculateFileChecksum(pool_file_);
        }
        if (std::filesystem::exists(index_file_)) {
            metadata_.checksums["index"] = CalculateFileChecksum(index_file_);
        }
        if (std::filesystem::exists(s2_index_file_)) {
            metadata_.checksums["s2_index"] = CalculateFileChecksum(s2_index_file_);
        }
    }

    void GisStorageSystem::UpdateMetadataStats() {
        if (geometry_storage_) {
            metadata_.total_features = geometry_storage_->GetAllFeatureIds().size();
            metadata_.valid_features = metadata_.total_features;
        }

        if (s2_spatial_index_) {
            metadata_.s2_index_info = "S2索引大小: " + std::to_string(GetS2IndexSize()) + ", 要素数量: " + std::to_string(GetS2TotalFeatureCount());
        }
    }

    // 文件路径获取方法
    std::string GisStorageSystem::GetGeometryFilePath() const {
        return geom_file_;
    }
    std::string GisStorageSystem::GetAttributeFilePath() const {
        return attr_file_;
    }
    std::string GisStorageSystem::GetStringPoolFilePath() const {
        return pool_file_;
    }
    std::string GisStorageSystem::GetIndexFilePath() const {
        return index_file_;
    }
    std::string GisStorageSystem::GetMetadataFilePath() const {
        return metadata_file_;
    }
    std::string GisStorageSystem::GetS2IndexFilePath() const {
        return s2_index_file_;
    }

    // 存储统计信息
    GisStorageSystem::StorageStats GisStorageSystem::GetStorageStats() const {
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
    std::string GisStorageSystem::CalculateFileChecksum(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }

        // 简单的MD5计算（这里使用简化版本）
        std::stringstream ss;
        ss << std::hex << std::hash<std::string>{}(file_path + std::to_string(std::filesystem::file_size(file_path)));
        return ss.str();
    }

    std::string GisStorageSystem::GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

} // namespace GisStorage
