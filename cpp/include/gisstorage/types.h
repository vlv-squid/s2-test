//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstddef>

namespace GisStorage {

    // 几何数据类型枚举
    enum class GeometryType : uint8_t { POINT = 0, LINE = 1, POLYGON = 2, MULTIPOINT = 3, MULTILINE = 4, MULTIPOLYGON = 5 };

    // 坐标点结构
    struct Coordinate {
        double x;
        double y;

        Coordinate(double x = 0.0, double y = 0.0)
            : x(x)
            , y(y) {}
    };

    // 边界框结构
    struct BBox {
        double min_x;
        double min_y;
        double max_x;
        double max_y;

        BBox(double min_x = 0.0, double min_y = 0.0, double max_x = 0.0, double max_y = 0.0)
            : min_x(min_x)
            , min_y(min_y)
            , max_x(max_x)
            , max_y(max_y) {}

        bool isValid() const { return min_x <= max_x && min_y <= max_y; }
    };

} // namespace GisStorage

#endif // TYPES_H
