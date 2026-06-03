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
        static std::vector<uint8_t> SerializeGeometry(const GeometryData& geometry);

        // 反序列化几何数据
        static std::unique_ptr<GeometryData> DeserializeGeometry(const std::vector<uint8_t>& data);

        // 序列化坐标数据
        static std::vector<uint8_t> SerializeCoordinates(const std::vector<Coordinate>& coordinates);

        // 反序列化坐标数据
        static std::vector<Coordinate> DeserializeCoordinates(const std::vector<uint8_t>& data);

        // 差分编码坐标压缩
        static std::vector<uint8_t> EncodeCoordinatesDelta(const std::vector<Coordinate>& coordinates);

        // 差分编码坐标解压
        static std::vector<Coordinate> DecodeCoordinatesDelta(const std::vector<uint8_t>& data);

        // 多环多边形序列化
        static std::vector<uint8_t> SerializeMultiRingPolygon(const std::vector<std::vector<Coordinate>>& rings);

        // 多环多边形反序列化
        static std::vector<std::vector<Coordinate>> DeserializeMultiRingPolygon(const std::vector<uint8_t>& data);

        // 计算边界框
        static BBox CalculateBBox(const std::vector<Coordinate>& coordinates);
    };

} // namespace GisStorage

#endif // GEOMETRY_SERIALIZER_H
