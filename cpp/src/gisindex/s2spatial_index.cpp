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

}; // namespace S2Main