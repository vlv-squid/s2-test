//
//  Created by vlv-squid on 2025.07.24.
//

#include "gisindex/rtree_spatial_index.h"
#include "gisindex/serialize_rtree.h"

#include <iostream>
#include <filesystem>

namespace S2Main {

    RtreeSpatialIndex::RtreeSpatialIndex(const std::string& filePath)
        : filePath_(filePath)
        , rtree_(nullptr) {}

    RtreeSpatialIndex::~RtreeSpatialIndex() {
        delete rtree_;
    }

    void RtreeSpatialIndex::build(const std::vector<RtreeValue>& values) {
        delete rtree_;
        rtree_ = new bgi::rtree<RtreeValue, bgi::quadratic<16>>(values);
    }

    void RtreeSpatialIndex::save() const {
        if (rtree_ && !helper::saveRtreeToFile(filePath_.c_str(), rtree_)) {
            std::cerr << "R 树保存失败: " << filePath_ << std::endl;
        }
    }

    void RtreeSpatialIndex::load() {
        delete rtree_;
        rtree_ = new bgi::rtree<RtreeValue, bgi::quadratic<16>>();
        if (!helper::loadRtreeFromFile(filePath_.c_str(), *rtree_)) {
            delete rtree_;
            rtree_ = nullptr;
        }
    }

    std::vector<RtreeValue> RtreeSpatialIndex::query(const Box& bbox) const {
        std::vector<RtreeValue> results;
        if (rtree_) {
            rtree_->query(bgi::intersects(bbox), std::back_inserter(results));
        }
        return results;
    }

    bool RtreeSpatialIndex::exists() const {
        return std::filesystem::exists(filePath_);
    }

} // namespace S2Main