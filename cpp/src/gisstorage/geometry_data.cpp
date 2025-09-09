//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_data.h"
#include "gisstorage/geometry_serializer.h"

#include <cstring>

namespace GisStorage {

    // GeometryData 实现
    GeometryData::GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox)
        : feature_id_(feature_id)
        , geometry_type_(geometry_type)
        , coordinates_(coordinates)
        , bbox_(bbox) {}

    std::vector<Coordinate> GeometryData::decodeCoordinates() const {
        // 使用GeometrySerializer的解码方法，避免重复实现
        return GeometrySerializer::decodeCoordinatesDelta(coordinates_);
    }

    size_t GeometryData::getSerializedSize() const {
        // feature_id(8) + geometry_type(1) + 7字节填充 + bbox(32) + coord_size(4) + coordinates
        return 8 + 1 + 7 + 32 + 4 + coordinates_.size();
    }

} // namespace GisStorage