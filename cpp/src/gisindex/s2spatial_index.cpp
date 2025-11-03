//
//  Created by vlv-squid on 2025.07.18.
//  优化版本：使用absl数据结构提升性能
//

#include "gisindex/s2spatial_index.h"
#include "gisindex/serialize_s2.h"

#include <iostream>
#include <filesystem>
#include <cmath>
#include <set>
#include <gdal.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <thread>
#include <tbb/tbb.h>

namespace S2Main {
    namespace {
        std::mutex g_s2_index_cache_mutex;
        std::unordered_map<std::string, std::weak_ptr<const absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>>> g_s2_index_cache;
    } // namespace

    S2SpatialIndex::S2SpatialIndex(const std::string& filePath, int level)
        : filePath_(filePath)
        , level_(level) {
        query_result_cache_.reserve(1000);
    }

    S2SpatialIndex::~S2SpatialIndex() = default;

    void S2SpatialIndex::Build(const std::vector<std::pair<int64_t, int>>& entries) {
        indexMap_.clear();
        AddBatch(entries);
    }

    void S2SpatialIndex::AddBatch(const std::vector<std::pair<int64_t, int>>& entries) {
        for (const auto& [cellId, fid] : entries) {
            indexMap_[cellId].push_back(fid);
        }
    }

    void S2SpatialIndex::Clear() {
        indexMap_.clear();
        query_result_cache_.clear();
        last_query_size_ = 0;
    }

    void S2SpatialIndex::Save() const {
        if (!helper::SaveS2IndexToFile(filePath_, indexMap_)) {
            std::cerr << "S2 索引保存失败: " << filePath_ << std::endl;
        }
    }

    void S2SpatialIndex::Load() {
        {
            std::lock_guard<std::mutex> lk(g_s2_index_cache_mutex);
            auto it = g_s2_index_cache.find(filePath_);
            if (it != g_s2_index_cache.end()) {
                auto shared = it->second.lock();
                if (shared) {
                    shared_index_map_ = shared;
                }
            }
        }

        static std::mutex s_load_once_mutex;
        static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> s_load_once_flags;

        std::once_flag* once_flag = nullptr;
        {
            std::lock_guard<std::mutex> lk(s_load_once_mutex);
            auto it = s_load_once_flags.find(filePath_);
            if (it == s_load_once_flags.end()) {
                s_load_once_flags[filePath_] = std::make_unique<std::once_flag>();
                once_flag = s_load_once_flags[filePath_].get();
            } else {
                once_flag = it->second.get();
            }
        }

        static std::mutex s_shared_storage_mutex;
        static std::unordered_map<std::string, std::shared_ptr<const absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>>> s_shared_storage;

        const std::string file_path = filePath_;
        std::call_once(*once_flag, [file_path]() {
            std::cout << "[首次加载] 开始加载S2索引: " << file_path << std::endl;

            absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>> temp;
            if (!helper::LoadS2IndexFromFile(file_path, temp)) {
                std::cerr << "[首次加载] S2索引加载失败: " << file_path << std::endl;
                return;
            }

            auto shared = std::make_shared<const absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>>(std::move(temp));
            {
                std::lock_guard<std::mutex> lk(s_shared_storage_mutex);
                s_shared_storage[file_path] = shared;
            }
            {
                std::lock_guard<std::mutex> lk(g_s2_index_cache_mutex);
                g_s2_index_cache[file_path] = shared;
                std::cout << "[首次加载] S2索引加载完成: " << file_path << " (条目数: " << shared->size() << ")" << std::endl;
            }
        });

        {
            std::lock_guard<std::mutex> storage_lk(s_shared_storage_mutex);
            auto storage_it = s_shared_storage.find(filePath_);
            if (storage_it != s_shared_storage.end()) {
                shared_index_map_ = storage_it->second;
                {
                    std::lock_guard<std::mutex> lk(g_s2_index_cache_mutex);
                    g_s2_index_cache[filePath_] = storage_it->second;
                }
                return;
            } else {
                std::cerr << "[警告] S2SpatialIndex::Load: 静态存储中没有找到文件路径: " << filePath_ << std::endl;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_s2_index_cache_mutex);
            auto it = g_s2_index_cache.find(filePath_);
            if (it != g_s2_index_cache.end()) {
                auto shared = it->second.lock();
                if (shared) {
                    shared_index_map_ = shared;
                    {
                        std::lock_guard<std::mutex> storage_lk(s_shared_storage_mutex);
                        s_shared_storage[filePath_] = shared;
                    }
                    std::cout << "[调试] S2SpatialIndex::Load: 从缓存获取共享视图成功, 大小=" << shared->size() << std::endl;
                    return;
                } else {
                    std::cerr << "[警告] S2SpatialIndex::Load: 缓存中的weak_ptr已失效" << std::endl;
                }
            } else {
                std::cerr << "[警告] S2SpatialIndex::Load: 缓存中没有找到文件路径: " << filePath_ << std::endl;
            }
        }

        std::cerr << "[错误] S2SpatialIndex::Load: 无法获取共享视图，filePath_=" << filePath_ << std::endl;
    }

    std::vector<int> S2SpatialIndex::Query(const S2LatLngRect& rect, int level) const {
        if (query_result_cache_.capacity() < last_query_size_ * 2) {
            query_result_cache_.reserve(last_query_size_ * 2);
        }
        query_result_cache_.clear();

        S2RegionCoverer::Options options;
        options.set_min_level(level);
        options.set_max_level(level);
        options.set_max_cells(8);

        S2RegionCoverer coverer(options);
        std::vector<S2CellId> cellIds;
        coverer.GetCovering(rect, &cellIds);

        std::set<int> unique_fids;
        absl::Span<const S2CellId> cell_span(cellIds);
        const auto* map_ptr = shared_index_map_ ? shared_index_map_.get() : &indexMap_;
        for (const auto& cellId : cell_span) {
            auto it = map_ptr->find(cellId.id());
            if (it != map_ptr->end()) {
                const auto& fids = it->second;
                for (int fid : fids) {
                    unique_fids.insert(fid);
                }
            }
        }

        query_result_cache_.assign(unique_fids.begin(), unique_fids.end());
        last_query_size_ = query_result_cache_.size();
        return query_result_cache_;
    }

    void S2SpatialIndex::QueryBatch(const std::vector<S2LatLngRect>& rects, int level, std::vector<std::vector<int>>& results) const {
        results.clear();
        results.resize(rects.size());

        for (auto& result : results) {
            result.reserve(100); // 预分配合理的容量
        }

        tbb::parallel_for(tbb::blocked_range<size_t>(0, rects.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                results[i] = Query(rects[i], level);
            }
        });
    }

    bool S2SpatialIndex::Exists() const {
        return std::filesystem::exists(filePath_);
    }

    bool S2SpatialIndex::SmartLoadOrBuild(const std::string& dataset_path, int batch_size) {
        if (Exists() && IsIndexValid()) {
            std::cout << "索引文件有效，直接加载使用" << std::endl;
            Load();
            return true;
        }

        std::cout << "索引文件无效或不存在，开始重新构建..." << std::endl;
        return BuildFromDataset(dataset_path, batch_size);
    }

    bool S2SpatialIndex::IsIndexValid() const {
        if (shared_index_map_ && shared_index_map_->size() > 0) {
            return true;
        }

        if (!indexMap_.empty()) {
            return true;
        }

        if (!Exists()) {
            return false;
        }

        std::filesystem::path index_path(filePath_);
        try {
            auto file_size = std::filesystem::file_size(index_path);
            return file_size > 1024; // 索引文件大小大于1KB
        } catch (const std::exception& e) {
            return false;
        }
    }

    size_t S2SpatialIndex::GetIndexSize() const {
        if (shared_index_map_) {
            return shared_index_map_->size();
        }
        return indexMap_.size();
    }

    size_t S2SpatialIndex::GetTotalFeatureCount() const {
        std::set<int> unique_fids;
        for (const auto& [cell_id, fids] : indexMap_) {
            for (int fid : fids) {
                unique_fids.insert(fid);
            }
        }
        return unique_fids.size();
    }

    bool S2SpatialIndex::BuildFromDataset(const std::string& dataset_path, int batch_size) {
        GDALAllRegister();

        GDALDataset* poDS = static_cast<GDALDataset*>(GDALOpenEx(dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!poDS) {
            std::cerr << "无法打开数据集: " << dataset_path << std::endl;
            return false;
        }

        OGRLayer* poLayer = poDS->GetLayer(0);
        if (!poLayer) {
            std::cerr << "无法获取图层" << std::endl;
            GDALClose(poDS);
            return false;
        }

        int64_t total_features = poLayer->GetFeatureCount();
        std::cout << "开始构建S2索引，总要素数量: " << total_features << std::endl;

        std::vector<std::pair<int64_t, int>> batch_entries;
        batch_entries.reserve(batch_size);

        size_t processed_count = 0;
        size_t total_entries = 0;
        bool first_batch = true;

        poLayer->ResetReading();

        std::filesystem::path index_path(filePath_);
        std::filesystem::create_directories(index_path.parent_path());

        while (OGRFeature* poFeature = poLayer->GetNextFeature()) {
            OGRGeometry* poGeometry = poFeature->GetGeometryRef();
            if (poGeometry) {
                OGREnvelope envelope;
                poGeometry->getEnvelope(&envelope);

                if (envelope.MinX < envelope.MaxX && envelope.MinY < envelope.MaxY) {
                    S2LatLng p1 = S2LatLng::FromDegrees(envelope.MinY, envelope.MinX);
                    S2LatLng p2 = S2LatLng::FromDegrees(envelope.MaxY, envelope.MaxX);
                    S2LatLngRect rect(p1, p2);

                    S2RegionCoverer::Options options;
                    options.set_min_level(level_);
                    options.set_max_level(level_);
                    options.set_max_cells(8);
                    S2RegionCoverer coverer(options);

                    std::vector<S2CellId> cellIds;
                    coverer.GetCovering(rect, &cellIds);

                    for (const auto& cellId : cellIds) {
                        batch_entries.emplace_back(cellId.id(), poFeature->GetFID());
                    }
                }
            }

            processed_count++;

            if (batch_entries.size() >= batch_size || processed_count == total_features) {
                if (first_batch) {
                    Build(batch_entries);
                    first_batch = false;
                } else {
                    AddBatch(batch_entries);
                }
                total_entries += batch_entries.size();

                if (processed_count % 500000 == 0 || processed_count == total_features) {
                    double progress = (double)processed_count / total_features * 100.0;
                    std::cout << "\r进度: " << std::fixed << std::setprecision(1) << progress << "% (" << processed_count << "/" << total_features << "), 已处理索引条目: " << total_entries << std::flush;
                }

                batch_entries.clear();
                batch_entries.reserve(batch_size);
            }

            OGRFeature::DestroyFeature(poFeature);
        }

        Save();

        GDALClose(poDS);
        std::cout << "S2索引构建完成，总处理要素: " << processed_count << ", 总索引条目: " << total_entries << std::endl;
        return true;
    }

    bool S2SpatialIndex::BuildFromDatasetMultiThreaded(const std::string& dataset_path, int batch_size, int num_threads) {
        GDALAllRegister();

        GDALDataset* poDS = static_cast<GDALDataset*>(GDALOpenEx(dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!poDS) {
            std::cerr << "无法打开数据集: " << dataset_path << std::endl;
            std::cerr << "GDAL错误: " << CPLGetLastErrorMsg() << std::endl;
            return false;
        }

        OGRLayer* poLayer = poDS->GetLayer(0);
        if (!poLayer) {
            std::cerr << "无法获取图层" << std::endl;
            GDALClose(poDS);
            return false;
        }

        int64_t total_features = poLayer->GetFeatureCount();
        std::cout << "开始多线程构建S2索引，总要素数量: " << total_features << ", 线程数: " << num_threads << std::endl;

        std::filesystem::path index_path(filePath_);
        std::filesystem::create_directories(index_path.parent_path());

        std::cout << "收集要素ID..." << std::endl;
        std::vector<int> valid_fids;
        valid_fids.reserve(total_features);

        poLayer->ResetReading();
        OGRFeature* poFeature;
        int64_t processed = 0;

        while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
            OGRGeometry* poGeometry = poFeature->GetGeometryRef();
            if (poGeometry && !poGeometry->IsEmpty()) {
                valid_fids.push_back(poFeature->GetFID());
            }
            OGRFeature::DestroyFeature(poFeature);

            processed++;
            if (processed % 1000000 == 0) {
                std::cout << "已收集 " << processed << " 个要素ID" << std::endl;
            }
        }

        std::cout << "收集完成，有效要素数量: " << valid_fids.size() << std::endl;

        size_t features_per_thread = valid_fids.size() / num_threads;
        size_t remaining_features = valid_fids.size() % num_threads;

        std::vector<std::vector<std::pair<int64_t, int>>> thread_results(num_threads);

        auto processFeatures = [&](int thread_id, size_t start_idx, size_t end_idx) {
            std::vector<std::pair<int64_t, int>> local_entries;
            local_entries.reserve(batch_size);

            GDALDataset* threadDS = static_cast<GDALDataset*>(GDALOpenEx(dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
            if (!threadDS) {
                std::cerr << "线程 " << thread_id << " 无法打开数据集" << std::endl;
                return;
            }

            OGRLayer* threadLayer = threadDS->GetLayer(0);
            if (!threadLayer) {
                std::cerr << "线程 " << thread_id << " 无法获取图层" << std::endl;
                GDALClose(threadDS);
                return;
            }

            for (size_t i = start_idx; i < end_idx; ++i) {
                int fid = valid_fids[i];

                OGRFeature* poFeature = threadLayer->GetFeature(fid);
                if (!poFeature)
                    continue;

                OGRGeometry* poGeometry = poFeature->GetGeometryRef();
                if (poGeometry && !poGeometry->IsEmpty()) {
                    try {
                        OGREnvelope envelope;
                        poGeometry->getEnvelope(&envelope);

                        if (envelope.MinX < envelope.MaxX && envelope.MinY < envelope.MaxY) {
                            S2LatLng p1 = S2LatLng::FromDegrees(envelope.MinY, envelope.MinX);
                            S2LatLng p2 = S2LatLng::FromDegrees(envelope.MaxY, envelope.MaxX);
                            S2LatLngRect rect(p1, p2);

                            S2RegionCoverer::Options options;
                            options.set_min_level(level_);
                            options.set_max_level(level_);
                            options.set_max_cells(8);
                            S2RegionCoverer coverer(options);

                            std::vector<S2CellId> cellIds;
                            coverer.GetCovering(rect, &cellIds);

                            for (const auto& cellId : cellIds) {
                                local_entries.emplace_back(cellId.id(), fid);
                            }
                        }
                    } catch (const std::exception& e) {
                        continue;
                    }
                }

                OGRFeature::DestroyFeature(poFeature);

                if ((i - start_idx + 1) % 100000 == 0) {
                    double progress = (double)(i - start_idx + 1) / (end_idx - start_idx) * 100.0;
                    std::cout << "\r线程 " << thread_id << " 进度: " << std::fixed << std::setprecision(1) << progress << "% (" << (i - start_idx + 1) << "/" << (end_idx - start_idx) << ")" << std::flush;
                }
            }

            GDALClose(threadDS);

            thread_results[thread_id] = std::move(local_entries);
        };

        std::vector<std::thread> threads;
        size_t current_start = 0;

        for (int i = 0; i < num_threads; ++i) {
            size_t current_end = current_start + features_per_thread;
            if (i < remaining_features) {
                current_end++;
            }

            threads.emplace_back(processFeatures, i, current_start, current_end);
            current_start = current_end;
        }

        for (auto& thread : threads) {
            thread.join();
        }

        std::cout << "合并线程结果..." << std::endl;
        size_t total_entries = 0;

        for (int i = 0; i < num_threads; ++i) {
            total_entries += thread_results[i].size();
            std::cout << "线程 " << i << " 处理了 " << thread_results[i].size() << " 个要素" << std::endl;
        }

        std::cout << "构建最终索引..." << std::endl;
        indexMap_.clear();

        for (const auto& thread_result : thread_results) {
            for (const auto& [cell_id, fid] : thread_result) {
                indexMap_[cell_id].push_back(fid);
            }
        }

        Save();

        GDALClose(poDS);
        std::cout << "多线程S2索引构建完成，总处理要素: " << valid_fids.size() << ", 总索引条目: " << total_entries << std::endl;
        return true;
    }
}; // namespace S2Main
