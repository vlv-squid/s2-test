//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_data.h"
#include "gisstorage/geometry_serializer.h"

#include <cstring>

namespace GisStorage {

    // GeometryData 实现
    GeometryData::GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox, uint32_t num_rings)
        : feature_id_(feature_id)
        , geometry_type_(geometry_type)
        , coordinates_(coordinates)
        , bbox_(bbox)
        , num_rings_(num_rings) {}

    std::vector<Coordinate> GeometryData::DecodeCoordinates() const {
        // 对于多边形，使用多环解码方法
        if (geometry_type_ == GeometryType::POLYGON && num_rings_ > 0) {
            auto rings = GeometrySerializer::DeserializeMultiRingPolygon(coordinates_);
            // 将所有环的坐标合并（保持向后兼容）
            std::vector<Coordinate> all_coordinates;
            for (const auto& ring : rings) {
                all_coordinates.insert(all_coordinates.end(), ring.begin(), ring.end());
            }
            return all_coordinates;
        } else {
            // 对于非多边形或单环多边形，使用原有的解码方法
            return GeometrySerializer::DecodeCoordinatesDelta(coordinates_);
        }
    }

    std::vector<std::vector<Coordinate>> GeometryData::DecodeMultiRingCoordinates() const {
        // 对于多边形，返回多环数据
        if (geometry_type_ == GeometryType::POLYGON && num_rings_ > 0) {
            return GeometrySerializer::DeserializeMultiRingPolygon(coordinates_);
        } else {
            // 对于非多边形，返回单环数据
            auto coords = GeometrySerializer::DecodeCoordinatesDelta(coordinates_);
            return {coords};
        }
    }

    size_t GeometryData::GetSerializedSize() const {
        // feature_id(8) + geometry_type(1) + 7字节填充 + bbox(32) + num_rings(4) + coord_size(4) + coordinates
        return 8 + 1 + 7 + 32 + 4 + 4 + coordinates_.size();
    }

} // namespace GisStorage