//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef GEOMETRY_SERIALIZER_H
#define GEOMETRY_SERIALIZER_H

#include "types.h"
#include "geometry_data.h"

#include <vector>
#include <memory>

namespace GisStorage {

    // 序列化器类
    class GeometrySerializer {
      public:
        // 序列化几何数据
        static std::vector<uint8_t> serializeGeometry(const GeometryData& geometry);

        // 反序列化几何数据
        static std::unique_ptr<GeometryData> deserializeGeometry(const std::vector<uint8_t>& data);

        // 序列化坐标数据
        static std::vector<uint8_t> serializeCoordinates(const std::vector<Coordinate>& coordinates);

        // 反序列化坐标数据
        static std::vector<Coordinate> deserializeCoordinates(const std::vector<uint8_t>& data);

        // 差分编码坐标压缩
        static std::vector<uint8_t> encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates);

        // 差分编码坐标解压
        static std::vector<Coordinate> decodeCoordinatesDelta(const std::vector<uint8_t>& data);

        // 计算边界框
        static BBox calculateBBox(const std::vector<Coordinate>& coordinates);
    };

} // namespace GisStorage

#endif // GEOMETRY_SERIALIZER_H
