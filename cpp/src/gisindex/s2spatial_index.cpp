//
//  Created by vlv-squid on 2025.07.18.
//

#include "gisindex/s2spatial_index.h"
#include "gisindex/serialize_s2.h"

#include <iostream>
#include <filesystem>
#include <cmath>
#include <gdal.h>
#include <cpl_conv.h>

namespace S2Main {

    S2SpatialIndex::S2SpatialIndex(const std::string& filePath, IndexStorageType storageType, int level)
        : filePath_(filePath)
        , storageType_(storageType)
        , level_(level) {}

    S2SpatialIndex::~S2SpatialIndex() = default;

    void S2SpatialIndex::build(const std::vector<std::pair<int64_t, int>>& entries) {
        indexMap_.clear();
        for (const auto& [cellId, fid] : entries) {
            indexMap_[cellId].push_back(fid);
        }
    }

    void S2SpatialIndex::save() const {
        if (storageType_ == IndexStorageType::BINARY_FILE) {
            if (!helper::saveS2IndexToFile(filePath_, indexMap_)) {
                std::cerr << "S2 索引保存失败: " << filePath_ << std::endl;
            }
        }
    }

    void S2SpatialIndex::load() {
        if (storageType_ == IndexStorageType::BINARY_FILE) {
            if (!helper::loadS2IndexFromFile(filePath_, indexMap_)) {
                indexMap_.clear();
            }
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

}; // namespace S2Main