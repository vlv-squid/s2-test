#include "hybrid_index.h"

#include <iostream>
#include <cmath>
#include <gdal.h>
#include <cpl_conv.h>
#include <ogr_api.h>

namespace S2Main {

    HybridIndex::HybridIndex(const std::string& dataPath, const std::string& indexDir, int s2Level)
        : dataPath_(dataPath)
        , indexDir_(indexDir)
        , s2Level_(s2Level) {
        std::string dataName = dataPath.substr(dataPath.find_last_of('/') + 1, dataPath.find_last_of('.') - dataPath.find_last_of('/') - 1);
        rtreeFilePath_ = indexDir_ + "/" + dataName + "_rtree.idx";
        s2BinaryFilePath_ = indexDir_ + "/" + dataName + "_s2.idx";

        rtreeIndex_ = std::make_unique<RtreeSpatialIndex>(rtreeFilePath_);
        s2Index_ = std::make_unique<S2SpatialIndex>(s2BinaryFilePath_, IndexStorageType::BINARY_FILE, s2Level_);
    }

    HybridIndex::~HybridIndex() = default;

    void HybridIndex::buildIndex(bool rebuild) {
        if (!rebuild && rtreeIndex_->exists() && s2Index_->exists()) {
            std::cout << "索引已存在，直接加载..." << std::endl;
            rtreeIndex_->load();
            s2Index_->load();
            return;
        }

        std::cout << "开始构建索引..." << std::endl;
        buildFromGDAL();
    }

    void HybridIndex::buildFromGDAL() {
        GDALDatasetH poDataset = GDALOpenEx(dataPath_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!poDataset) {
            std::cerr << "无法打开数据源: " << dataPath_ << std::endl;
            return;
        }

        OGRLayerH poLayer = GDALDatasetGetLayer(poDataset, 0);
        if (!poLayer) {
            std::cerr << "无法获取图层" << std::endl;
            GDALClose(poDataset);
            return;
        }

        std::vector<RtreeValue> rtreeValues;
        std::vector<std::pair<int64_t, int>> s2Entries;

        OGRFeatureH feature;
        while ((feature = OGR_L_GetNextFeature(poLayer))) {
            int fid = OGR_F_GetFID(feature);
            OGRGeometryH geom = OGR_F_GetGeometryRef(feature);
            if (!geom) {
                OGR_F_Destroy(feature);
                continue;
            }

            OGREnvelope env;
            OGR_G_GetEnvelope(geom, &env);
            Box bounds(Point(env.MinX, env.MinY), Point(env.MaxX, env.MaxY));
            featureBounds_[fid] = bounds;
            rtreeValues.emplace_back(bounds, fid);

            S2LatLng p1 = S2LatLng::FromDegrees(env.MinY, env.MinX);
            S2LatLng p2 = S2LatLng::FromDegrees(env.MaxY, env.MaxX);
            S2LatLngRect rect(p1, p2);

            S2RegionCoverer::Options options;
            options.set_min_level(s2Level_);
            options.set_max_level(s2Level_);
            options.set_max_cells(8);

            S2RegionCoverer coverer(options);
            std::vector<S2CellId> cellIds;
            coverer.GetCovering(rect, &cellIds);
            for (const auto& cellId : cellIds) {
                s2Entries.emplace_back(cellId.id(), fid);
            }

            OGR_F_Destroy(feature);
        }

        rtreeIndex_->build(rtreeValues);
        rtreeIndex_->save();

        s2Index_->build(s2Entries);
        s2Index_->save();

        GDALClose(poDataset);
    }

    std::vector<int> HybridIndex::queryByBbox(const Box& bbox, bool useRtree, bool exactCheck) {
        std::vector<int> results;

        // S2 查询
        const auto& env = bg::return_envelope<Box>(bbox);
        const auto& min_corner = env.min_corner();
        const auto& max_corner = env.max_corner();

        S2LatLng p1 = S2LatLng::FromDegrees(bg::get<1>(min_corner), bg::get<0>(min_corner));
        S2LatLng p2 = S2LatLng::FromDegrees(bg::get<1>(max_corner), bg::get<0>(max_corner));
        S2LatLngRect rect(p1, p2);

        int autoLevel = calculateAutoLevel(bbox);
        int useLevel = std::min(s2Level_, autoLevel);

        auto s2Results = s2Index_->query(rect, useLevel);

        std::set<int> candidateFids(s2Results.begin(), s2Results.end());

        // R-tree 过滤
        if (useRtree) {
            auto rtreeResults = rtreeIndex_->query(bbox);
            std::set<int> rtreeFids;
            for (const auto& v : rtreeResults)
                rtreeFids.insert(v.second);

            std::set<int> intersection;
            std::set_intersection(candidateFids.begin(), candidateFids.end(), rtreeFids.begin(), rtreeFids.end(), std::inserter(intersection, intersection.begin()));
            candidateFids = intersection;
        }

        // 精确检查
        if (exactCheck) {
            loadFeatureBoundsIfNeeded();
            for (int fid : candidateFids) {
                if (intersectRect(featureBounds_[fid], bbox)) {
                    results.push_back(fid);
                }
            }
        } else {
            results.assign(candidateFids.begin(), candidateFids.end());
        }

        return results;
    }

    void HybridIndex::loadFeatureBoundsIfNeeded() {
        if (!featureBounds_.empty())
            return;

        GDALDatasetH poDataset = GDALOpenEx(dataPath_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!poDataset)
            return;

        OGRLayerH poLayer = GDALDatasetGetLayer(poDataset, 0);
        if (!poLayer) {
            GDALClose(poDataset);
            return;
        }

        OGRFeatureH feature;
        while ((feature = OGR_L_GetNextFeature(poLayer))) {
            int fid = OGR_F_GetFID(feature);
            OGRGeometryH geom = OGR_F_GetGeometryRef(feature);
            if (geom) {
                OGREnvelope env;
                OGR_G_GetEnvelope(geom, &env);
                Box bounds(Point(env.MinX, env.MinY), Point(env.MaxX, env.MaxY));
                featureBounds_[fid] = bounds;
            }
            OGR_F_Destroy(feature);
        }

        GDALClose(poDataset);
    }

    int HybridIndex::calculateAutoLevel(const Box& bbox) {
        double area = bg::area(bbox);
        return std::min(30, std::max(10, 30 - static_cast<int>(std::log10(area * 10000))));
    }

    bool HybridIndex::intersectRect(const Box& a, const Box& b) {
        return !bg::disjoint(a, b);
    }

} // namespace S2Main