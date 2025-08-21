#ifndef GEOMETRY_DATA_H
#define GEOMETRY_DATA_H

#include "types.h"
#include <vector>
#include <cstdint>

namespace GisStorage {

    // 几何数据类
    class GeometryData {
      public:
        GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox);

        uint64_t getFeatureId() const { return feature_id_; }
        GeometryType getGeometryType() const { return geometry_type_; }
        const std::vector<uint8_t>& getCoordinates() const { return coordinates_; }
        const BBox& getBBox() const { return bbox_; }

        // 解码压缩的坐标数据
        std::vector<Coordinate> decodeCoordinates() const;

        // 计算序列化后的大小
        size_t getSerializedSize() const;

      private:
        uint64_t feature_id_;
        GeometryType geometry_type_;
        std::vector<uint8_t> coordinates_; // 压缩的坐标数据
        BBox bbox_;
    };

} // namespace GisStorage

#endif // GEOMETRY_DATA_H
