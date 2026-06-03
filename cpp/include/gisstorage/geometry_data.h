//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef GEOMETRY_DATA_H
#define GEOMETRY_DATA_H

#include "types.h"

#include <vector>
#include <cstdint>

namespace GisStorage {

    // 几何数据类
    class GeometryData {
      public:
        GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox, uint32_t num_rings = 0);

        uint64_t GetFeatureId() const { return feature_id_; }
        GeometryType GetGeometryType() const { return geometry_type_; }
        const std::vector<uint8_t>& GetCoordinates() const { return coordinates_; }
        const BBox& GetBBox() const { return bbox_; }
        uint32_t GetNumRings() const { return num_rings_; }

        // 解码压缩的坐标数据
        std::vector<Coordinate> DecodeCoordinates() const;

        // 解码多环多边形数据
        std::vector<std::vector<Coordinate>> DecodeMultiRingCoordinates() const;

        // 计算序列化后的大小
        size_t GetSerializedSize() const;

      private:
        uint64_t feature_id_;
        GeometryType geometry_type_;
        std::vector<uint8_t> coordinates_; // 压缩的坐标数据
        BBox bbox_;
        uint32_t num_rings_; // 多边形的环数量（外环+内环）
    };

} // namespace GisStorage

#endif // GEOMETRY_DATA_H
