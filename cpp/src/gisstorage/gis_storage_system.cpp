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

    GisStorageSystem::GisStorageSystem(const std::string& output_dir)
        : output_dir_(output_dir) {
        std::filesystem::create_directories(output_dir);
        metadata_.creation_date = GetCurrentTimestamp();
    }

    std::unique_ptr<GeometryData> GisStorageSystem::ReadGeometry(uint64_t feature_id) {
        if (!geometry_storage_) {
            InitializeFilePaths();
            geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
            geometry_storage_->SetChunkSize(10000);
            geometry_storage_->SetCacheSize(1000);
        }
        return geometry_storage_->ReadGeometry(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::ReadAttribute(uint64_t feature_id) {
        if (!attribute_storage_) {
            InitializeFilePaths();
            attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
            attribute_storage_->SetChunkSize(10000);
            attribute_storage_->SetCacheSize(1000);
            attribute_storage_->SetUseMmapMode(true);
        }
        return attribute_storage_->ReadAttribute(feature_id);
    }

    std::unique_ptr<GeometryData> GisStorageSystem::ReadGeometryOnDemand(uint64_t feature_id) {
        if (!geometry_storage_) {
            InitializeFilePaths();
            geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
            geometry_storage_->SetChunkSize(10000);
            geometry_storage_->SetCacheSize(1000);
        }
        return geometry_storage_->ReadGeometryOnDemand(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::ReadAttributeOnDemand(uint64_t feature_id) {
        if (!attribute_storage_) {
            InitializeFilePaths();
            attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);

            attribute_storage_->SetChunkSize(10000);
            attribute_storage_->SetCacheSize(1000);
            attribute_storage_->SetUseMmapMode(true);
        }
        return attribute_storage_->ReadAttributeOnDemand(feature_id);
    }

    std::vector<uint64_t> GisStorageSystem::GetAllFeatureIds() {
        if (attribute_storage_ && attribute_storage_->IsChunkedMode()) {
            return attribute_storage_->GetAllFeatureIds();
        }

        if (geometry_storage_ && geometry_storage_->IsChunkedMode()) {
            return geometry_storage_->GetAllFeatureIds();
        }

        const auto& metadata = GetMetadata();
        size_t valid_features = metadata.valid_features;

        if (valid_features == 0) {
            valid_features = metadata.total_features;
        }

        std::vector<uint64_t> feature_ids;
        feature_ids.reserve(valid_features);
        for (size_t i = 0; i < valid_features; ++i) {
            feature_ids.push_back(i);
        }

        return feature_ids;
    }

    std::vector<uint64_t> GisStorageSystem::QueryByAttributeEfficient(const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;
        if (!attribute_storage_) {
            try {
                InitializeFilePaths();
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                attribute_storage_->SetChunkSize(10000);
                attribute_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      基于字符串池的高效属性查询：字段='" << field_name << "', 值='" << field_value << "'" << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();
            auto stats = attribute_storage_->GetCompressionStats();
            std::cout << "      字符串池包含 " << stats.unique_strings << " 个唯一字符串" << std::endl;
            const auto& metadata = GetMetadata();
            size_t total_features = metadata.total_features;
            std::cout << "      将查询 " << total_features << " 个要素（按需读取优化）" << std::endl;

            const size_t batch_size = 1000;
            size_t processed = 0;
            size_t found_count = 0;
            for (size_t i = 1; i <= total_features; i += batch_size) {
                size_t end_fid = std::min(i + batch_size - 1, total_features);

                for (uint64_t fid = i; fid <= end_fid; ++fid) {
                    try {
                        auto attr = attribute_storage_->ReadAttributeOnDemand(fid);
                        if (attr) {
                            auto value_opt = attr->GetProperty(field_name);
                            bool match = false;
                            if (field_value == "NULL") {
                                // 查询NULL值的属性
                                match = !value_opt.has_value();
                            } else if (field_value == "NOT_NULL") {
                                // 查询非NULL值的属性
                                match = value_opt.has_value();
                            } else {
                                // 正常值比较
                                std::string value = attr->GetProperty(field_name, "");
                                match = (value == field_value);
                            }
                            if (match) {
                                results.push_back(fid);
                                found_count++;
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }
                }

                processed += batch_size;
                if (processed % 10000 == 0) {
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

        if (!attribute_storage_) {
            try {
                InitializeFilePaths();
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                attribute_storage_->SetChunkSize(10000);
                attribute_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      并行属性查询：字段='" << field_name << "', 值='" << field_value << "'" << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();
            auto stats = attribute_storage_->GetCompressionStats();
            std::cout << "      字符串池包含 " << stats.unique_strings << " 个唯一字符串" << std::endl;
            const auto& metadata = GetMetadata();
            size_t total_features = metadata.total_features;
            std::cout << "      将并行查询 " << total_features << " 个要素" << std::endl;

            tbb::concurrent_vector<uint64_t> concurrent_results;
            std::atomic<size_t> processed_count{0};
            std::atomic<size_t> found_count{0};
            std::mutex progress_mutex;

            tbb::parallel_for(tbb::blocked_range<size_t>(1, total_features + 1), [&](const tbb::blocked_range<size_t>& range) {
                std::vector<uint64_t> local_results;
                local_results.reserve(1000);

                for (size_t fid = range.begin(); fid != range.end(); ++fid) {
                    try {
                        auto attr = attribute_storage_->ReadAttributeOnDemand(fid);
                        if (attr) {
                            auto value_opt = attr->GetProperty(field_name);
                            bool match = false;
                            if (field_value == "NULL") {
                                // 查询NULL值的属性
                                match = !value_opt.has_value();
                            } else if (field_value == "NOT_NULL") {
                                // 查询非NULL值的属性
                                match = value_opt.has_value();
                            } else {
                                // 正常值比较
                                std::string value = attr->GetProperty(field_name, "");
                                match = (value == field_value);
                            }
                            if (match) {
                                local_results.push_back(fid);
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }

                    size_t current_processed = processed_count.fetch_add(1) + 1;
                    if (current_processed % 100000 == 0) {
                        std::lock_guard<std::mutex> lock(progress_mutex);
                        auto current_time = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                        double speed = current_processed / (elapsed.count() / 1000.0);
                        std::cout << "\r      进度: " << current_processed << "/" << total_features << " (" << (100.0 * current_processed / total_features) << "%) " << "找到: " << found_count.load()
                                  << " 速度: " << static_cast<int>(speed) << " 要素/秒" << std::flush;
                    }
                }

                if (!local_results.empty()) {
                    for (const auto& fid : local_results) {
                        concurrent_results.push_back(fid);
                    }
                    found_count.fetch_add(local_results.size());
                }
            });

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
        if (!attribute_storage_) {
            try {
                InitializeFilePaths();
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                attribute_storage_->SetChunkSize(10000);
                attribute_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      高效复合查询：空间范围=[" << spatial_bbox.min_x << "," << spatial_bbox.min_y << " - " << spatial_bbox.max_x << "," << spatial_bbox.max_y << "], 字段='" << field_name << "', 值='"
                      << field_value << "'" << std::endl;

            auto start_time = std::chrono::high_resolution_clock::now();
            auto spatial_results = QueryS2Index(spatial_bbox);
            std::cout << "      空间查询找到 " << spatial_results.size() << " 个候选要素" << std::endl;

            if (spatial_results.empty()) {
                std::cout << "      空间查询无结果，复合查询完成" << std::endl;
                return results;
            }

            const size_t batch_size = 1000;
            size_t processed = 0;
            size_t found_count = 0;
            for (size_t i = 0; i < spatial_results.size(); i += batch_size) {
                size_t end_idx = std::min(i + batch_size, spatial_results.size());

                for (size_t j = i; j < end_idx; ++j) {
                    uint64_t fid = spatial_results[j];
                    try {
                        auto attr = attribute_storage_->ReadAttributeOnDemand(fid);
                        if (attr) {
                            auto value_opt = attr->GetProperty(field_name);
                            bool match = false;
                            if (field_value == "NULL") {
                                // 查询NULL值的属性
                                match = !value_opt.has_value();
                            } else if (field_value == "NOT_NULL") {
                                // 查询非NULL值的属性
                                match = value_opt.has_value();
                            } else {
                                // 正常值比较
                                std::string value = attr->GetProperty(field_name, "");
                                match = (value == field_value);
                            }
                            if (match) {
                                results.push_back(fid);
                                found_count++;
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }
                }

                processed += (end_idx - i);
                if (processed % 5000 == 0 || processed == spatial_results.size()) {
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

    std::vector<uint64_t> GisStorageSystem::QuerySpatialAttributeStreaming(const BBox& spatial_bbox, const std::string& field_name, const std::string& field_value) {
        std::vector<uint64_t> results;

        try {
            std::cout << "      流式复合查询：空间范围=[" << spatial_bbox.min_x << "," << spatial_bbox.min_y << " - " << spatial_bbox.max_x << "," << spatial_bbox.max_y << "], 字段='" << field_name << "', 值='"
                      << field_value << "'" << std::endl;

            auto start_time = std::chrono::high_resolution_clock::now();
            auto spatial_candidates = QuerySpatialCandidates(spatial_bbox);
            std::cout << "      空间查询找到 " << spatial_candidates.size() << " 个候选要素" << std::endl;

            if (spatial_candidates.empty()) {
                std::cout << "      空间查询无结果，流式查询完成" << std::endl;
                return results;
            }

            const size_t batch_size = 1000;
            size_t processed = 0;
            size_t found_count = 0;
            for (size_t i = 0; i < spatial_candidates.size(); i += batch_size) {
                size_t end_idx = std::min(i + batch_size, spatial_candidates.size());

                for (size_t j = i; j < end_idx; ++j) {
                    uint64_t fid = spatial_candidates[j];
                    auto attr = attribute_storage_ ? attribute_storage_->ReadAttributeOnDemand(fid) : nullptr;
                    if (attr) {
                        auto value_opt = attr->GetProperty(field_name);
                        bool match = false;
                        if (field_value == "NULL") {
                            match = !value_opt.has_value();
                        } else if (field_value == "NOT_NULL") {
                            match = value_opt.has_value();
                        } else {
                            std::string value = attr->GetProperty(field_name, "");
                            match = (value == field_value);
                        }
                        if (match) {
                            results.push_back(fid);
                            found_count++;
                        }
                    }

                    processed++;
                }

                if (processed % 5000 == 0 || processed == spatial_candidates.size()) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    std::cout << "      流式查询进度: " << processed << "/" << spatial_candidates.size() << " (" << (processed * 100 / spatial_candidates.size()) << "%), " << "找到 " << found_count
                              << " 个匹配要素, 耗时 " << elapsed.count() << "ms" << std::endl;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      流式复合查询完成: 处理 " << processed << " 个要素, 找到 " << found_count << " 个匹配要素, 总耗时 " << total_elapsed.count() << "ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "流式复合查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QuerySpatialCandidates(const BBox& spatial_bbox) {
        if (!s2_spatial_index_ || !IsS2IndexValid()) {
            return {};
        }

        try {
            S2LatLng p1 = S2LatLng::FromDegrees(spatial_bbox.min_y, spatial_bbox.min_x);
            S2LatLng p2 = S2LatLng::FromDegrees(spatial_bbox.max_y, spatial_bbox.max_x);
            S2LatLngRect rect(p1, p2);
            auto s2_results = s2_spatial_index_->Query(rect, 15);
            std::vector<uint64_t> results;
            results.reserve(s2_results.size());
            for (int fid : s2_results) {
                results.push_back(static_cast<uint64_t>(fid));
            }

            return results;

        } catch (const std::exception& e) {
            std::cerr << "S2候选查询错误: " << e.what() << std::endl;
            return {};
        }
    }

    std::vector<uint64_t> GisStorageSystem::QueryByAttributePattern(const std::string& field_name, const std::string& pattern) {
        std::vector<uint64_t> results;
        if (!attribute_storage_) {
            try {
                InitializeFilePaths();
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                attribute_storage_->SetChunkSize(10000);
                attribute_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = this->GetAllFeatureIds();
            } else {
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->ReadAttributeOnDemand(fid);
                    if (attr) {
                        auto value_opt = attr->GetProperty(field_name);
                        if (value_opt.has_value()) {
                            std::string value = value_opt.value();
                            if (value.find(pattern) != std::string::npos) {
                                results.push_back(fid);
                            }
                        }
                    }
                } catch (const std::exception& e) {
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
        if (!attribute_storage_) {
            try {
                InitializeFilePaths();
                attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
                attribute_storage_->SetChunkSize(10000);
                attribute_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "属性存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = this->GetAllFeatureIds();
            } else {
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            for (uint64_t fid : all_feature_ids) {
                try {
                    auto attr = attribute_storage_->ReadAttributeOnDemand(fid);
                    if (attr) {
                        auto value_opt = attr->GetProperty(field_name);
                        if (value_opt.has_value() && !value_opt.value().empty()) {
                            results.emplace_back(fid, value_opt.value());
                        }
                    }
                } catch (const std::exception& e) {
                    continue;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "属性值查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    void GisStorageSystem::InitializeStorageFiles(const std::string& shapefile_path) {
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();
        InitializeFilePaths();
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
        CreateMetadataFromShapefile(shapefile_path);
        geometry_storage_->SetChunkSize(10000);
        geometry_storage_->SetCacheSize(1000);
        attribute_storage_->SetChunkSize(10000);
        attribute_storage_->SetCacheSize(10000);
        attribute_storage_->SetUseMmapMode(true);

        if (std::filesystem::exists(geom_chunked_index_file_)) {
            geometry_storage_->LoadChunkedIndex(geom_chunked_index_file_);
        }

        if (std::filesystem::exists(attr_chunked_index_file_)) {
            attribute_storage_->LoadChunkedIndex(attr_chunked_index_file_);
        }
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }

        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::SetDatasetName(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;
        InitializeFilePaths();
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
        geometry_storage_->SetChunkSize(10000);
        geometry_storage_->SetCacheSize(1000);
        attribute_storage_->SetChunkSize(10000);
        attribute_storage_->SetCacheSize(10000);
        attribute_storage_->SetUseMmapMode(true);
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }

        if (std::filesystem::exists(s2_index_file_)) {
            InitializeS2Index(15);
            s2_spatial_index_->Load();
        }

        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::SetDatasetNameLightweight(const std::string& dataset_name) {
        shapefile_name_ = dataset_name;
        InitializeFilePaths();
        PreloadChunkedIndexes();
        if (std::filesystem::exists(metadata_file_)) {
            LoadMetadata();
        }
        UpdateFileSizes();
        UpdateChecksums();
    }

    void GisStorageSystem::PreloadChunkedIndexes() {
        static std::mutex s_preload_once_mutex;
        static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> s_preload_once;

        std::string dataset_key = output_dir_ + "/" + shapefile_name_;
        {
            std::lock_guard<std::mutex> g(s_preload_once_mutex);
            if (s_preload_once.find(dataset_key) == s_preload_once.end()) {
                s_preload_once[dataset_key] = std::make_unique<std::once_flag>();
            }
        }

        std::once_flag& once = *s_preload_once[dataset_key];
        std::call_once(once, [dataset_key, this]() {
            std::cout << "[预加载] 开始预加载数据集资源: " << dataset_key << " (线程ID: " << std::this_thread::get_id() << ")" << std::endl;
            if (std::filesystem::exists(geom_chunked_index_file_)) {
                std::cout << "[预加载] 预加载几何分块索引文件: " << geom_chunked_index_file_ << std::endl;
                GeometryStorage temp_geom(geom_file_);
                temp_geom.SetChunkSize(10000);
                temp_geom.SetCacheSize(1000);
                temp_geom.LoadChunkedIndex(geom_chunked_index_file_);
                std::cout << "[预加载] 几何分块索引预加载完成" << std::endl;
            }

            if (std::filesystem::exists(attr_chunked_index_file_)) {
                std::cout << "[预加载] 预加载属性分块索引文件: " << attr_chunked_index_file_ << std::endl;
                AttributeStorage temp_attr(attr_file_, pool_file_);
                temp_attr.SetChunkSize(10000);
                temp_attr.SetCacheSize(1000);
                temp_attr.SetUseMmapMode(true);
                if (std::filesystem::exists(pool_file_)) {
                    std::cout << "[预加载] 预加载字符串池文件: " << pool_file_ << std::endl;
                    temp_attr.LoadStringPool();
                    std::cout << "[预加载] 字符串池预加载完成" << std::endl;
                }

                temp_attr.LoadChunkedIndex(attr_chunked_index_file_);
                std::cout << "[预加载] 属性分块索引预加载完成" << std::endl;
            }

            if (std::filesystem::exists(s2_index_file_)) {
                std::cout << "[预加载] 预加载S2索引文件: " << s2_index_file_ << std::endl;
                S2Main::S2SpatialIndex temp_s2(s2_index_file_, 15);
                temp_s2.Load();
                std::cout << "[预加载] S2索引预加载完成" << std::endl;
            }

            std::cout << "[预加载] 数据集资源预加载完成: " << dataset_key << std::endl;
        });

        if (!geometry_storage_ && std::filesystem::exists(geom_chunked_index_file_)) {
            geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
            geometry_storage_->SetChunkSize(10000);
            geometry_storage_->SetCacheSize(1000);
            geometry_storage_->LoadChunkedIndex(geom_chunked_index_file_);
        }

        if (!attribute_storage_ && std::filesystem::exists(attr_chunked_index_file_)) {
            attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_, pool_file_);
            attribute_storage_->SetChunkSize(10000);
            attribute_storage_->SetCacheSize(1000);
            attribute_storage_->SetUseMmapMode(true);
            if (std::filesystem::exists(pool_file_)) {
                attribute_storage_->LoadStringPool();
            }
            attribute_storage_->LoadChunkedIndex(attr_chunked_index_file_);
        }

        if (!s2_spatial_index_ && std::filesystem::exists(s2_index_file_)) {
            InitializeS2Index(15);
            s2_spatial_index_->Load();
        }
    }

    void GisStorageSystem::InitializeFilePaths() {
        geom_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::GEOMETRY_DATA;
        attr_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::ATTRIBUTE_DATA;
        pool_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::STRING_POOL;
        geom_chunked_index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::GEOMETRY_CHUNKED_INDEX;
        attr_chunked_index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::ATTRIBUTE_CHUNKED_INDEX;
        metadata_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::METADATA;
        s2_index_file_ = output_dir_ + "/" + shapefile_name_ + FileExtensions::S2_INDEX;
    }

    void GisStorageSystem::CreateMetadataFromShapefile(const std::string& shapefile_path) {
        metadata_.source_file = std::filesystem::path(shapefile_path).filename().string();
        metadata_.source_format = "Shapefile";
        metadata_.creation_date = GetCurrentTimestamp();
    }

    void GisStorageSystem::InitializeS2Index(int resolution) {
        if (!s2_spatial_index_) {
            s2_spatial_index_ = new S2Main::S2SpatialIndex(s2_index_file_, resolution);
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
            S2LatLng p1 = S2LatLng::FromDegrees(query_bbox.min_y, query_bbox.min_x); // 纬度, 经度
            S2LatLng p2 = S2LatLng::FromDegrees(query_bbox.max_y, query_bbox.max_x);
            S2LatLngRect rect(p1, p2);
            auto s2_results = s2_spatial_index_->Query(rect, resolution);
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

    std::vector<uint64_t> GisStorageSystem::QueryByBBox(const BBox& query_bbox) {
        std::vector<uint64_t> results;
        if (!geometry_storage_) {
            try {
                InitializeFilePaths();
                geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
                geometry_storage_->SetChunkSize(10000);
                geometry_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "几何存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      基于bbox的空间查询：范围=[" << query_bbox.min_x << "," << query_bbox.min_y << " - " << query_bbox.max_x << "," << query_bbox.max_y << "]" << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = this->GetAllFeatureIds();
            } else {
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }
            std::cout << "      将查询 " << all_feature_ids.size() << " 个要素的bbox" << std::endl;
            const size_t batch_size = 1000;
            size_t processed = 0;
            size_t found_count = 0;

            for (size_t i = 0; i < all_feature_ids.size(); i += batch_size) {
                size_t end_idx = std::min(i + batch_size, all_feature_ids.size());
                for (size_t j = i; j < end_idx; ++j) {
                    uint64_t fid = all_feature_ids[j];
                    try {
                        auto geom = geometry_storage_->ReadGeometryOnDemand(fid);
                        if (geom) {
                            const BBox& feature_bbox = geom->GetBBox();
                            if (IsBBoxIntersecting(query_bbox, feature_bbox)) {
                                results.push_back(fid);
                                found_count++;
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }
                }
                processed += (end_idx - i);

                if (processed % 5000 == 0 || processed == all_feature_ids.size()) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    double speed = processed / (elapsed.count() / 1000.0);
                    std::cout << "\r      进度: " << processed << "/" << all_feature_ids.size() << " (" << (100.0 * processed / all_feature_ids.size()) << "%) " << "找到: " << found_count
                              << " 速度: " << static_cast<int>(speed) << " 要素/秒" << std::flush;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      bbox查询完成，找到 " << results.size() << " 个相交要素，耗时 " << total_elapsed.count() << " ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "bbox查询错误: " << e.what() << std::endl;
        }

        return results;
    }

    std::vector<uint64_t> GisStorageSystem::QueryByBBoxParallel(const BBox& query_bbox) {
        std::vector<uint64_t> results;
        if (!geometry_storage_) {
            try {
                InitializeFilePaths();
                geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_);
                geometry_storage_->SetChunkSize(10000);
                geometry_storage_->SetCacheSize(1000);
            } catch (const std::exception& e) {
                std::cerr << "几何存储初始化失败: " << e.what() << std::endl;
                return results;
            }
        }

        try {
            std::cout << "      并行bbox空间查询：范围=[" << query_bbox.min_x << "," << query_bbox.min_y << " - " << query_bbox.max_x << "," << query_bbox.max_y << "]" << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();
            std::vector<uint64_t> all_feature_ids;
            if (geometry_storage_) {
                all_feature_ids = this->GetAllFeatureIds();
            } else {
                const auto& metadata = GetMetadata();
                size_t total_features = metadata.total_features;
                all_feature_ids.reserve(total_features);
                for (size_t i = 1; i <= total_features; ++i) {
                    all_feature_ids.push_back(i);
                }
            }

            std::cout << "      将并行查询 " << all_feature_ids.size() << " 个要素的bbox" << std::endl;
            tbb::concurrent_vector<uint64_t> concurrent_results;
            std::atomic<size_t> processed_count{0};
            std::atomic<size_t> found_count{0};
            std::mutex progress_mutex;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, all_feature_ids.size()), [&](const tbb::blocked_range<size_t>& range) {
                std::vector<uint64_t> local_results;
                local_results.reserve(1000);

                for (size_t i = range.begin(); i != range.end(); ++i) {
                    uint64_t fid = all_feature_ids[i];
                    try {
                        auto geom = geometry_storage_->ReadGeometryOnDemand(fid);
                        if (geom) {
                            const BBox& feature_bbox = geom->GetBBox();
                            if (IsBBoxIntersecting(query_bbox, feature_bbox)) {
                                local_results.push_back(fid);
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }

                    size_t current_processed = processed_count.fetch_add(1) + 1;
                    if (current_processed % 100000 == 0) {
                        std::lock_guard<std::mutex> lock(progress_mutex);
                        auto current_time = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                        double speed = current_processed / (elapsed.count() / 1000.0);
                        std::cout << "\r      进度: " << current_processed << "/" << all_feature_ids.size() << " (" << (100.0 * current_processed / all_feature_ids.size()) << "%) " << "找到: " << found_count.load()
                                  << " 速度: " << static_cast<int>(speed) << " 要素/秒" << std::flush;
                    }
                }

                if (!local_results.empty()) {
                    for (const auto& fid : local_results) {
                        concurrent_results.push_back(fid);
                    }
                    found_count.fetch_add(local_results.size());
                }
            });

            results.reserve(concurrent_results.size());
            for (const auto& fid : concurrent_results) {
                results.push_back(fid);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "      并行bbox查询完成，找到 " << results.size() << " 个相交要素，耗时 " << total_elapsed.count() << " ms" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "并行bbox查询错误: " << e.what() << std::endl;
        }

        return results;
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

    void GisStorageSystem::UpdateMetadata(const Metadata& metadata) {
        metadata_ = metadata;
        SaveMetadata();
    }

    void GisStorageSystem::SaveMetadata() {
        nlohmann::json metadata_json;
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
        if (!metadata_json.contains("compression_info") || metadata_json["compression_info"].empty()) {
            if (!metadata_.compression_info.empty()) {
                try {
                    nlohmann::json compression_json = nlohmann::json::parse(metadata_.compression_info);
                    metadata_json["compression_info"] = compression_json;
                } catch (const std::exception&) {
                    metadata_json["compression_info"] = metadata_.compression_info;
                }
            } else {
                metadata_json["compression_info"] = "";
            }
        }
        metadata_json["s2_index_info"] = metadata_.s2_index_info;
        if (!metadata_json.contains("field_definitions") || metadata_json["field_definitions"].empty()) {
            metadata_json["field_definitions"] = metadata_.field_definitions;
        }
        if (!metadata_json.contains("spatial_extent") ||
            (metadata_json["spatial_extent"]["min_x"] == 0.0 && metadata_json["spatial_extent"]["min_y"] == 0.0 && metadata_json["spatial_extent"]["max_x"] == 0.0 && metadata_json["spatial_extent"]["max_y"] == 0.0) ||
            (std::abs(static_cast<double>(metadata_json["spatial_extent"]["min_x"])) > 1e9 || std::abs(static_cast<double>(metadata_json["spatial_extent"]["min_y"])) > 1e9 ||
             std::abs(static_cast<double>(metadata_json["spatial_extent"]["max_x"])) > 1e9 || std::abs(static_cast<double>(metadata_json["spatial_extent"]["max_y"])) > 1e9)) {
            if (std::abs(metadata_.spatial_extent.min_x) > 1e9 || std::abs(metadata_.spatial_extent.min_y) > 1e9 || std::abs(metadata_.spatial_extent.max_x) > 1e9 || std::abs(metadata_.spatial_extent.max_y) > 1e9) {
                nlohmann::json spatial_extent_json;
                spatial_extent_json["min_x"] = metadata_.spatial_extent.min_x;
                spatial_extent_json["min_y"] = metadata_.spatial_extent.min_y;
                spatial_extent_json["max_x"] = metadata_.spatial_extent.max_x;
                spatial_extent_json["max_y"] = metadata_.spatial_extent.max_y;
                metadata_json["spatial_extent"] = spatial_extent_json;
            }
        }

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

            metadata_.format_version = metadata_json.value("format_version", "1.0");
            metadata_.source_format = metadata_json.value("source_format", "Shapefile");
            metadata_.source_file = metadata_json.value("source_file", "");
            metadata_.creation_date = metadata_json.value("creation_date", "");
            metadata_.source_coordinate_system = metadata_json.value("source_coordinate_system", "");
            metadata_.target_coordinate_system = metadata_json.value("target_coordinate_system", "");
            metadata_.total_features = metadata_json.value("total_features", 0);
            metadata_.valid_features = metadata_json.value("valid_features", 0);
            if (metadata_json.contains("field_definitions")) {
                auto field_defs = metadata_json["field_definitions"];
                for (auto& [field_name, field_type] : field_defs.items()) {
                    if (field_type.is_string()) {
                        metadata_.field_definitions[field_name] = field_type.get<std::string>();
                    } else if (field_type.is_number()) {
                        metadata_.field_definitions[field_name] = std::to_string(field_type.get<int>());
                    } else {
                        metadata_.field_definitions[field_name] = field_type.dump();
                    }
                }
            }
            metadata_.file_sizes = metadata_json.value("file_sizes", std::map<std::string, size_t>{});
            metadata_.checksums = metadata_json.value("checksums", std::map<std::string, std::string>{});
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
        if (std::filesystem::exists(geom_chunked_index_file_)) {
            metadata_.file_sizes["geometry_chunked_index"] = std::filesystem::file_size(geom_chunked_index_file_);
        }
        if (std::filesystem::exists(attr_chunked_index_file_)) {
            metadata_.file_sizes["attribute_chunked_index"] = std::filesystem::file_size(attr_chunked_index_file_);
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
        if (std::filesystem::exists(geom_chunked_index_file_)) {
            metadata_.checksums["geometry_chunked_index"] = CalculateFileChecksum(geom_chunked_index_file_);
        }
        if (std::filesystem::exists(attr_chunked_index_file_)) {
            metadata_.checksums["attribute_chunked_index"] = CalculateFileChecksum(attr_chunked_index_file_);
        }
        if (std::filesystem::exists(s2_index_file_)) {
            metadata_.checksums["s2_index"] = CalculateFileChecksum(s2_index_file_);
        }
    }

    void GisStorageSystem::UpdateMetadataStats() {
        if (geometry_storage_) {
            metadata_.total_features = this->GetAllFeatureIds().size();
            metadata_.valid_features = metadata_.total_features;
        }

        if (s2_spatial_index_) {
            metadata_.s2_index_info = "S2索引大小: " + std::to_string(GetS2IndexSize()) + ", 要素数量: " + std::to_string(GetS2TotalFeatureCount());
        }
    }

    std::string GisStorageSystem::GetGeometryFilePath() const {
        return geom_file_;
    }
    std::string GisStorageSystem::GetAttributeFilePath() const {
        return attr_file_;
    }
    std::string GisStorageSystem::GetStringPoolFilePath() const {
        return pool_file_;
    }
    std::string GisStorageSystem::GetGeometryChunkedIndexFilePath() const {
        return geom_chunked_index_file_;
    }
    std::string GisStorageSystem::GetAttributeChunkedIndexFilePath() const {
        return attr_chunked_index_file_;
    }
    std::string GisStorageSystem::GetMetadataFilePath() const {
        return metadata_file_;
    }
    std::string GisStorageSystem::GetS2IndexFilePath() const {
        return s2_index_file_;
    }

    GisStorageSystem::StorageStats GisStorageSystem::GetStorageStats() const {
        StorageStats stats;
        std::vector<std::string> core_files = {"geometry", "attribute", "string_pool", "geometry_chunked_index", "attribute_chunked_index", "s2_index"};
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
                } else if (file_type == "geometry_chunked_index") {
                    stats.geometry_chunked_index_size_bytes = it->second;
                } else if (file_type == "attribute_chunked_index") {
                    stats.attribute_chunked_index_size_bytes = it->second;
                } else if (file_type == "s2_index") {
                    stats.s2_index_size_bytes = it->second;
                }
            } else {
                std::string file_path;
                if (file_type == "geometry") {
                    file_path = geom_file_;
                } else if (file_type == "attribute") {
                    file_path = attr_file_;
                } else if (file_type == "string_pool") {
                    file_path = pool_file_;
                } else if (file_type == "geometry_chunked_index") {
                    file_path = geom_chunked_index_file_;
                } else if (file_type == "attribute_chunked_index") {
                    file_path = attr_chunked_index_file_;
                } else if (file_type == "s2_index") {
                    file_path = s2_index_file_;
                }

                if (!file_path.empty() && std::filesystem::exists(file_path)) {
                    size_t file_size = std::filesystem::file_size(file_path);
                    stats.total_files++;
                    stats.total_size_bytes += file_size;

                    if (file_type == "geometry") {
                        stats.geometry_size_bytes = file_size;
                    } else if (file_type == "attribute") {
                        stats.attribute_size_bytes = file_size;
                    } else if (file_type == "string_pool") {
                        stats.string_pool_size_bytes = file_size;
                    } else if (file_type == "geometry_chunked_index") {
                        stats.geometry_chunked_index_size_bytes = file_size;
                    } else if (file_type == "attribute_chunked_index") {
                        stats.attribute_chunked_index_size_bytes = file_size;
                    } else if (file_type == "s2_index") {
                        stats.s2_index_size_bytes = file_size;
                    }
                }
            }
        }

        return stats;
    }

    std::string GisStorageSystem::CalculateFileChecksum(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }

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

    bool GisStorageSystem::IsBBoxIntersecting(const BBox& bbox1, const BBox& bbox2) const {
        return (bbox1.max_x >= bbox2.min_x && bbox1.min_x <= bbox2.max_x) && (bbox1.max_y >= bbox2.min_y && bbox1.min_y <= bbox2.max_y);
    }

    bool GisStorageSystem::ValidateFileStructure() {
        std::vector<std::string> required_files = {geom_file_, attr_file_, pool_file_, geom_chunked_index_file_, attr_chunked_index_file_, metadata_file_};
        for (const auto& file_path : required_files) {
            if (!std::filesystem::exists(file_path)) {
                std::cerr << "缺少必需文件: " << file_path << std::endl;
                return false;
            }
        }

        return true;
    }

    std::vector<std::string> GisStorageSystem::GetMissingFiles() {
        std::vector<std::string> missing_files;
        std::vector<std::string> required_files = {geom_file_, attr_file_, pool_file_, geom_chunked_index_file_, attr_chunked_index_file_, metadata_file_};

        for (const auto& file_path : required_files) {
            if (!std::filesystem::exists(file_path)) {
                missing_files.push_back(file_path);
            }
        }

        return missing_files;
    }

} // namespace GisStorage
