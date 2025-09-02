//
//  Created by vlv-squid on 2025.07.18.
//

#include "gisindex/s2spatial_index.h"
#include "gisindex/serialize_s2.h"

#include <iostream>
#include <filesystem>
#include <cmath>
#include <gdal.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <set>
#include <thread>
#include <tbb/tbb.h>

namespace S2Main {

    S2SpatialIndex::S2SpatialIndex(const std::string& filePath, int level)
        : filePath_(filePath)
        , level_(level) {}

    S2SpatialIndex::~S2SpatialIndex() = default;

    void S2SpatialIndex::build(const std::vector<std::pair<int64_t, int>>& entries) {
        indexMap_.clear();
        addBatch(entries);
    }

    void S2SpatialIndex::addBatch(const std::vector<std::pair<int64_t, int>>& entries) {
        for (const auto& [cellId, fid] : entries) {
            indexMap_[cellId].push_back(fid);
        }
    }

    void S2SpatialIndex::clear() {
        indexMap_.clear();
    }

    void S2SpatialIndex::save() const {
        if (!helper::saveS2IndexToFile(filePath_, indexMap_)) {
            std::cerr << "S2 索引保存失败: " << filePath_ << std::endl;
        }
    }

    void S2SpatialIndex::load() {
        if (!helper::loadS2IndexFromFile(filePath_, indexMap_)) {
            indexMap_.clear();
        }
    }

    std::vector<int> S2SpatialIndex::query(const S2LatLngRect& rect, int level) const {
        std::vector<int> result;
        S2RegionCoverer::Options options;
        options.set_min_level(level);
        options.set_max_level(level);
        options.set_max_cells(8);

        S2RegionCoverer coverer(options);
        std::vector<S2CellId> cellIds;
        coverer.GetCovering(rect, &cellIds);

        for (const auto& cellId : cellIds) {
            auto it = indexMap_.find(cellId.id());
            if (it != indexMap_.end()) {
                for (int fid : it->second) {
                    result.push_back(fid);
                }
            }
        }

        return result;
    }

    bool S2SpatialIndex::exists() const {
        return std::filesystem::exists(filePath_);
    }

    // 智能索引管理方法实现
    bool S2SpatialIndex::smartLoadOrBuild(const std::string& dataset_path, int batch_size) {
        // 首先尝试加载现有索引
        if (exists() && isIndexValid()) {
            std::cout << "索引文件有效，直接加载使用" << std::endl;
            load();
            return true;
        }

        // 如果索引无效或不存在，重新构建
        std::cout << "索引文件无效或不存在，开始重新构建..." << std::endl;
        return buildFromDataset(dataset_path, batch_size);
    }

    bool S2SpatialIndex::isIndexValid() const {
        if (!exists()) {
            return false;
        }

        // 检查索引文件大小是否合理
        std::filesystem::path index_path(filePath_);
        try {
            auto file_size = std::filesystem::file_size(index_path);
            return file_size > 1024; // 索引文件大小大于1KB
        } catch (const std::exception& e) {
            return false;
        }
    }

    size_t S2SpatialIndex::getIndexSize() const {
        return indexMap_.size();
    }

    bool S2SpatialIndex::buildFromDataset(const std::string& dataset_path, int batch_size) {
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

        // 获取总要素数量用于进度显示
        int64_t total_features = poLayer->GetFeatureCount();
        std::cout << "开始构建S2索引，总要素数量: " << total_features << std::endl;

        // 分批处理参数
        std::vector<std::pair<int64_t, int>> batch_entries;
        batch_entries.reserve(batch_size);

        // 统计信息
        size_t processed_count = 0;
        size_t total_entries = 0;

        poLayer->ResetReading();

        // 创建输出目录
        std::filesystem::path index_path(filePath_);
        std::filesystem::create_directories(index_path.parent_path());

        while (OGRFeature* poFeature = poLayer->GetNextFeature()) {
            OGRGeometry* poGeometry = poFeature->GetGeometryRef();
            if (poGeometry) {
                OGRPoint center;
                if (poGeometry->Centroid(&center) == OGRERR_NONE) {
                    // 将坐标转换为S2 Cell ID
                    S2LatLng latlng = S2LatLng::FromDegrees(center.getY(), center.getX());

                    // 使用S2RegionCoverer获取Cell ID
                    S2RegionCoverer::Options options;
                    options.set_min_level(level_);
                    options.set_max_level(level_);
                    options.set_max_cells(1);
                    S2RegionCoverer coverer(options);

                    // 创建一个小矩形区域
                    S2LatLng p1 = S2LatLng::FromDegrees(center.getY() - 0.0001, center.getX() - 0.0001);
                    S2LatLng p2 = S2LatLng::FromDegrees(center.getY() + 0.0001, center.getX() + 0.0001);
                    S2LatLngRect rect(p1, p2);

                    std::vector<S2CellId> cellIds;
                    coverer.GetCovering(rect, &cellIds);

                    if (!cellIds.empty()) {
                        S2CellId cellId = cellIds[0];
                        batch_entries.emplace_back(cellId.id(), poFeature->GetFID());
                    }
                }
            }

            processed_count++;

            // 当批次满了或者是最后一批时，处理当前批次
            if (batch_entries.size() >= batch_size || processed_count == total_features) {
                // 将当前批次添加到索引（增量构建）
                if (processed_count <= batch_size) {
                    // 第一批，清空并构建
                    build(batch_entries);
                } else {
                    // 后续批次，增量添加
                    addBatch(batch_entries);
                }
                total_entries += batch_entries.size();

                // 显示进度
                if (processed_count % 500000 == 0 || processed_count == total_features) {
                    double progress = (double)processed_count / total_features * 100.0;
                    std::cout << "进度: " << std::fixed << std::setprecision(1) << progress << "% (" << processed_count << "/" << total_features << "), 已处理索引条目: " << total_entries << std::endl;
                }

                // 清空当前批次，准备下一批
                batch_entries.clear();
                batch_entries.reserve(batch_size);
            }

            // 释放要素内存
            OGRFeature::DestroyFeature(poFeature);
        }

        // 保存最终索引
        save();

        GDALClose(poDS);
        std::cout << "S2索引构建完成，总处理要素: " << processed_count << ", 总索引条目: " << total_entries << std::endl;
        return true;
    }

    // 多线程构建S2索引
    bool S2SpatialIndex::buildFromDatasetMultiThreaded(const std::string& dataset_path, int batch_size, int num_threads) {
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

        // 获取总要素数量
        int64_t total_features = poLayer->GetFeatureCount();
        std::cout << "开始多线程构建S2索引，总要素数量: " << total_features << ", 线程数: " << num_threads << std::endl;

        // 创建输出目录
        std::filesystem::path index_path(filePath_);
        std::filesystem::create_directories(index_path.parent_path());

        // 策略：先收集所有有效的要素ID，然后分块处理
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

        // 计算每个线程处理的要素数量
        size_t features_per_thread = valid_fids.size() / num_threads;
        size_t remaining_features = valid_fids.size() % num_threads;

        // 存储所有线程的结果
        std::vector<std::vector<std::pair<int64_t, int>>> thread_results(num_threads);

        // 线程函数：处理指定范围的要素ID
        auto processFeatures = [&](int thread_id, size_t start_idx, size_t end_idx) {
            std::vector<std::pair<int64_t, int>> local_entries;
            local_entries.reserve(batch_size);

            // 为每个线程创建独立的数据集访问器
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

                // 通过FID获取要素
                OGRFeature* poFeature = threadLayer->GetFeature(fid);
                if (!poFeature)
                    continue;

                OGRGeometry* poGeometry = poFeature->GetGeometryRef();
                if (poGeometry && !poGeometry->IsEmpty()) {
                    try {
                        OGRPoint center;
                        if (poGeometry->Centroid(&center) == OGRERR_NONE) {
                            // 计算S2 Cell ID
                            S2LatLng latlng = S2LatLng::FromDegrees(center.getY(), center.getX());

                            S2RegionCoverer::Options options;
                            options.set_min_level(level_);
                            options.set_max_level(level_);
                            options.set_max_cells(1);
                            S2RegionCoverer coverer(options);

                            S2LatLng p1 = S2LatLng::FromDegrees(center.getY() - 0.0001, center.getX() - 0.0001);
                            S2LatLng p2 = S2LatLng::FromDegrees(center.getY() + 0.0001, center.getX() + 0.0001);
                            S2LatLngRect rect(p1, p2);

                            std::vector<S2CellId> cellIds;
                            coverer.GetCovering(rect, &cellIds);

                            if (!cellIds.empty()) {
                                local_entries.emplace_back(cellIds[0].id(), fid);
                            }
                        }
                    } catch (const std::exception& e) {
                        // 忽略几何错误，继续处理下一个要素
                        continue;
                    }
                }

                OGRFeature::DestroyFeature(poFeature);

                // 显示进度
                if ((i - start_idx + 1) % 100000 == 0) {
                    double progress = (double)(i - start_idx + 1) / (end_idx - start_idx) * 100.0;
                    std::cout << "线程 " << thread_id << " 进度: " << std::fixed << std::setprecision(1) << progress << "% (" << (i - start_idx + 1) << "/" << (end_idx - start_idx) << ")" << std::endl;
                }
            }

            GDALClose(threadDS);

            // 将结果存储到对应线程的结果向量中
            thread_results[thread_id] = std::move(local_entries);
        };

        // 创建并启动线程
        std::vector<std::thread> threads;
        size_t current_start = 0;

        for (int i = 0; i < num_threads; ++i) {
            size_t current_end = current_start + features_per_thread;
            if (i < remaining_features) {
                current_end++; // 分配剩余要素
            }

            threads.emplace_back(processFeatures, i, current_start, current_end);
            current_start = current_end;
        }

        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }

        // 合并所有线程的结果
        std::cout << "合并线程结果..." << std::endl;
        size_t total_entries = 0;

        for (int i = 0; i < num_threads; ++i) {
            total_entries += thread_results[i].size();
            std::cout << "线程 " << i << " 处理了 " << thread_results[i].size() << " 个要素" << std::endl;
        }

        // 构建最终索引
        std::cout << "构建最终索引..." << std::endl;
        indexMap_.clear();

        for (const auto& thread_result : thread_results) {
            for (const auto& [cell_id, fid] : thread_result) {
                indexMap_[cell_id].push_back(fid);
            }
        }

        // 保存索引
        save();

        GDALClose(poDS);
        std::cout << "多线程S2索引构建完成，总处理要素: " << valid_fids.size() << ", 总索引条目: " << total_entries << std::endl;
        return true;
    }

    // 使用TBB的优化多线程构建S2索引
    bool S2SpatialIndex::buildFromDatasetMultiThreadedTBB(const std::string& dataset_path, int batch_size, int num_threads) {
        // 设置TBB线程数
        tbb::global_control global_limit(tbb::global_control::max_allowed_parallelism, num_threads);

        std::cout << "开始TBB优化多线程构建S2索引，总要素数量: 16511241, 线程数: " << num_threads << std::endl;

        // 创建输出目录
        std::filesystem::path index_path(filePath_);
        std::filesystem::create_directories(index_path.parent_path());

        // 第一步：单线程读取所有几何数据到内存（避免多线程I/O竞争）
        std::cout << "单线程读取几何数据到内存..." << std::endl;

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

        // 获取总要素数量
        int64_t total_features = poLayer->GetFeatureCount();

        // 预分配内存，存储所有几何数据
        struct GeometryData {
            GIntBig fid; // 使用GIntBig类型匹配GDAL的FID类型
            double center_x;
            double center_y;
        };

        std::vector<GeometryData> geometry_data;
        geometry_data.reserve(total_features);

        poLayer->ResetReading();
        OGRFeature* poFeature;
        int64_t processed = 0;

        while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
            OGRGeometry* poGeometry = poFeature->GetGeometryRef();
            if (poGeometry && !poGeometry->IsEmpty()) {
                try {
                    OGRPoint center;
                    if (poGeometry->Centroid(&center) == OGRERR_NONE) {
                        geometry_data.push_back({poFeature->GetFID(), center.getX(), center.getY()});
                    }
                } catch (const std::exception& e) {
                    // 忽略几何错误，继续处理下一个要素
                    continue;
                }
            }
            OGRFeature::DestroyFeature(poFeature);

            processed++;
            if (processed % 1000000 == 0) {
                std::cout << "已读取 " << processed << " 个几何要素" << std::endl;
            }
        }

        std::cout << "几何数据读取完成，有效要素数量: " << geometry_data.size() << std::endl;
        GDALClose(poDS);

        // 第二步：使用TBB并行处理内存中的几何数据，构建S2索引
        std::cout << "使用TBB并行处理内存中的几何数据..." << std::endl;

        // 使用TBB的concurrent_unordered_map存储结果
        tbb::concurrent_unordered_map<int64_t, std::vector<int>> concurrent_index_map;

        // 并行处理几何数据（完全在内存中，无I/O操作）
        tbb::parallel_for(tbb::blocked_range<size_t>(0, geometry_data.size()), [&](const tbb::blocked_range<size_t>& range) {
            // 本地批处理，减少锁竞争
            const size_t local_batch_size = 1000;
            std::vector<std::pair<int64_t, int>> local_batch;
            local_batch.reserve(local_batch_size);

            for (size_t i = range.begin(); i < range.end(); ++i) {
                const auto& geom = geometry_data[i];

                try {
                    // 计算S2 Cell ID（完全在内存中）
                    S2LatLng latlng = S2LatLng::FromDegrees(geom.center_y, geom.center_x);

                    S2RegionCoverer::Options options;
                    options.set_min_level(level_);
                    options.set_max_level(level_);
                    options.set_max_cells(1);
                    S2RegionCoverer coverer(options);

                    S2LatLng p1 = S2LatLng::FromDegrees(geom.center_y - 0.0001, geom.center_x - 0.0001);
                    S2LatLng p2 = S2LatLng::FromDegrees(geom.center_y + 0.0001, geom.center_x + 0.0001);
                    S2LatLngRect rect(p1, p2);

                    std::vector<S2CellId> cellIds;
                    coverer.GetCovering(rect, &cellIds);

                    if (!cellIds.empty()) {
                        int64_t cell_id = cellIds[0].id();
                        local_batch.emplace_back(cell_id, static_cast<int>(geom.fid));
                    }
                } catch (const std::exception& e) {
                    // 忽略几何错误，继续处理下一个要素
                    continue;
                }

                // 批量添加到concurrent map，减少锁竞争
                if (local_batch.size() >= local_batch_size || i == range.end() - 1) {
                    for (const auto& [cell_id, fid] : local_batch) {
                        concurrent_index_map[cell_id].push_back(fid);
                    }
                    local_batch.clear();
                }
            }
        });

        // 第三步：将TBB结果转换为标准索引格式
        std::cout << "转换TBB结果到标准索引格式..." << std::endl;
        indexMap_.clear();

        for (const auto& [cell_id, fids] : concurrent_index_map) {
            indexMap_[cell_id] = fids;
        }

        // 保存索引
        save();

        std::cout << "TBB优化多线程S2索引构建完成，总处理要素: " << geometry_data.size() << ", 总索引条目: " << indexMap_.size() << std::endl;
        return true;
    }

}; // namespace S2Main