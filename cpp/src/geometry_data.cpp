#include "geometry_data.h"
#include <cstring>
#include <algorithm>

namespace GisStorage {

    // GeometryData 实现
    GeometryData::GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox)
        : feature_id_(feature_id)
        , geometry_type_(geometry_type)
        , coordinates_(coordinates)
        , bbox_(bbox) {}

    std::vector<Coordinate> GeometryData::decodeCoordinates() const {
        if (coordinates_.empty()) {
            return {};
        }

        // 如果是单点情况
        if (coordinates_.size() == 16) {
            double x, y;
            std::memcpy(&x, &coordinates_[0], sizeof(double));
            std::memcpy(&y, &coordinates_[8], sizeof(double));
            return {Coordinate(x, y)};
        }

        // 差分编码的情况
        if (coordinates_.size() < 17) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取第一个点的绝对坐标
        double x, y;
        std::memcpy(&x, &coordinates_[0], sizeof(double));
        std::memcpy(&y, &coordinates_[8], sizeof(double));
        coordinates.emplace_back(x, y);

        if (coordinates_.size() <= 16) {
            return coordinates;
        }

        // 读取数据类型标记
        uint8_t type_flag = coordinates_[16];
        size_t pos = 17;

        if (type_flag == 0) { // short类型
            while (pos + 4 <= coordinates_.size()) {
                int16_t dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(int16_t));
                std::memcpy(&dy, &coordinates_[pos + 2], sizeof(int16_t));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 4;
            }
        } else if (type_flag == 1) { // int类型
            while (pos + 8 <= coordinates_.size()) {
                int32_t dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(int32_t));
                std::memcpy(&dy, &coordinates_[pos + 4], sizeof(int32_t));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 8;
            }
        } else if (type_flag == 2) { // float类型
            while (pos + 8 <= coordinates_.size()) {
                float dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(float));
                std::memcpy(&dy, &coordinates_[pos + 4], sizeof(float));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 8;
            }
        }

        return coordinates;
    }

    size_t GeometryData::getSerializedSize() const {
        // feature_id(8) + geometry_type(1) + 7字节填充 + bbox(32) + coord_size(4) + coordinates
        return 8 + 1 + 7 + 32 + 4 + coordinates_.size();
    }

} // namespace GisStorage