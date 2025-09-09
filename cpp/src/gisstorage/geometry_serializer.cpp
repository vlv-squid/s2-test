//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_serializer.h"

#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace GisStorage {

    // 变长整数编码辅助函数（类似Protocol Buffers的varint）
    static void encodeVarint(std::vector<uint8_t>& data, int64_t value) {
        // 使用ZigZag编码处理负数
        uint64_t zigzag = (value << 1) ^ (value >> 63);

        while (zigzag >= 0x80) {
            data.push_back(static_cast<uint8_t>(zigzag | 0x80));
            zigzag >>= 7;
        }
        data.push_back(static_cast<uint8_t>(zigzag));
    }

    static int64_t decodeVarint(const std::vector<uint8_t>& data, size_t& offset) {
        uint64_t result = 0;
        int shift = 0;

        while (offset < data.size()) {
            uint8_t byte = data[offset++];
            result |= (static_cast<uint64_t>(byte & 0x7F) << shift);

            if ((byte & 0x80) == 0) {
                break;
            }
            shift += 7;
        }

        // ZigZag解码
        return static_cast<int64_t>((result >> 1) ^ (-(result & 1)));
    }

    // GeometrySerializer 实现
    std::vector<uint8_t> GeometrySerializer::serializeGeometry(const GeometryData& geometry) {
        std::vector<uint8_t> data;
        data.reserve(geometry.getSerializedSize());

        // 写入feature_id (8字节)
        uint64_t feature_id = geometry.getFeatureId();
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&feature_id), reinterpret_cast<uint8_t*>(&feature_id) + sizeof(uint64_t));

        // 写入geometry_type (1字节)
        uint8_t geom_type = static_cast<uint8_t>(geometry.getGeometryType());
        data.push_back(geom_type);

        // 写入7字节填充（与Python版本保持一致）
        std::vector<uint8_t> padding(7, 0);
        data.insert(data.end(), padding.begin(), padding.end());

        // 写入bbox (32字节)
        const BBox& bbox = geometry.getBBox();
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_x), reinterpret_cast<const uint8_t*>(&bbox.min_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_y), reinterpret_cast<const uint8_t*>(&bbox.min_y) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_x), reinterpret_cast<const uint8_t*>(&bbox.max_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_y), reinterpret_cast<const uint8_t*>(&bbox.max_y) + sizeof(double));

        // 写入坐标数据大小 (4字节)
        uint32_t coord_size = static_cast<uint32_t>(geometry.getCoordinates().size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));

        // 写入坐标数据
        const auto& coords = geometry.getCoordinates();
        data.insert(data.end(), coords.begin(), coords.end());

        return data;
    }

    std::unique_ptr<GeometryData> GeometrySerializer::deserializeGeometry(const std::vector<uint8_t>& data) {
        if (data.size() < 48) {
            throw std::runtime_error("数据长度不足，无法反序列化几何对象");
        }

        size_t offset = 0;

        // 读取feature_id
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // 读取geometry_type
        uint8_t geom_type_byte;
        std::memcpy(&geom_type_byte, &data[offset], sizeof(uint8_t));
        GeometryType geometry_type = static_cast<GeometryType>(geom_type_byte);
        offset += sizeof(uint8_t);

        // 跳过7字节填充（与Python版本保持一致）
        offset += 7;

        // 读取bbox
        BBox bbox;
        std::memcpy(&bbox.min_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.min_y, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_y, &data[offset], sizeof(double));
        offset += sizeof(double);

        // 读取坐标数据大小
        uint32_t coord_size;
        std::memcpy(&coord_size, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + coord_size > data.size()) {
            throw std::runtime_error("坐标数据不完整");
        }

        // 读取坐标数据
        std::vector<uint8_t> coordinates(data.begin() + offset, data.begin() + offset + coord_size);

        return std::make_unique<GeometryData>(feature_id, geometry_type, coordinates, bbox);
    }

    std::vector<uint8_t> GeometrySerializer::serializeCoordinates(const std::vector<Coordinate>& coordinates) {
        std::vector<uint8_t> data;
        data.reserve(4 + coordinates.size() * 16); // 4字节数量 + 每个坐标16字节

        // 写入点的数量
        uint32_t num_points = static_cast<uint32_t>(coordinates.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_points), reinterpret_cast<uint8_t*>(&num_points) + sizeof(uint32_t));

        // 写入所有坐标点
        for (const auto& coord : coordinates) {
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.x), reinterpret_cast<const uint8_t*>(&coord.x) + sizeof(double));
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.y), reinterpret_cast<const uint8_t*>(&coord.y) + sizeof(double));
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::deserializeCoordinates(const std::vector<uint8_t>& data) {
        if (data.size() < 4) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取点的数量
        uint32_t num_points;
        std::memcpy(&num_points, &data[0], sizeof(uint32_t));

        size_t offset = 4;
        coordinates.reserve(num_points);

        for (uint32_t i = 0; i < num_points && offset + 16 <= data.size(); ++i) {
            Coordinate coord;
            std::memcpy(&coord.x, &data[offset], sizeof(double));
            offset += sizeof(double);
            std::memcpy(&coord.y, &data[offset], sizeof(double));
            offset += sizeof(double);
            coordinates.push_back(coord);
        }

        return coordinates;
    }

    std::vector<uint8_t> GeometrySerializer::encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        if (coordinates.size() == 1) {
            // 单点情况，直接存储绝对坐标
            std::vector<uint8_t> data(16);
            std::memcpy(&data[0], &coordinates[0].x, sizeof(double));
            std::memcpy(&data[8], &coordinates[0].y, sizeof(double));
            return data;
        }

        // FileGDB风格的整型化和压缩
        // 1. 计算数据的空间范围
        double min_x = coordinates[0].x, max_x = coordinates[0].x;
        double min_y = coordinates[0].y, max_y = coordinates[0].y;

        for (const auto& coord : coordinates) {
            min_x = std::min(min_x, coord.x);
            max_x = std::max(max_x, coord.x);
            min_y = std::min(min_y, coord.y);
            max_y = std::max(max_y, coord.y);
        }

        // 2. 计算分辨率，保持足够的精度
        double scale;
        if (max_x == min_x && max_y == min_y) {
            // 所有点都相同，使用固定分辨率
            scale = 1e-9;
        } else {
            // 计算合适的分辨率，确保整型化后不会溢出
            double range_x = max_x - min_x;
            double range_y = max_y - min_y;
            double max_range = std::max(range_x, range_y);

            // 使用1e9作为整型化范围，确保精度
            scale = std::max(1e-9, max_range / 1e9);
        }

        // 3. 整型化坐标并计算差分
        std::vector<std::pair<int64_t, int64_t>> int_coords;
        std::vector<std::pair<int64_t, int64_t>> deltas;

        for (const auto& coord : coordinates) {
            int64_t x_int = static_cast<int64_t>((coord.x - min_x) / scale);
            int64_t y_int = static_cast<int64_t>((coord.y - min_y) / scale);
            int_coords.emplace_back(x_int, y_int);
        }

        // 计算差分（第一个点存储绝对值，后续点存储差值）
        deltas.emplace_back(int_coords[0].first, int_coords[0].second);
        for (size_t i = 1; i < int_coords.size(); ++i) {
            deltas.emplace_back(int_coords[i].first - int_coords[i - 1].first, int_coords[i].second - int_coords[i - 1].second);
        }

        // 4. 存储数据：偏移量、分辨率、第一个点坐标、差分数据
        std::vector<uint8_t> data;
        data.reserve(32 + deltas.size() * 4); // 预估大小

        // 存储偏移量（16字节）
        data.resize(16);
        std::memcpy(&data[0], &min_x, sizeof(double));
        std::memcpy(&data[8], &min_y, sizeof(double));

        // 存储分辨率（8字节）
        data.resize(24);
        std::memcpy(&data[16], &scale, sizeof(double));

        // 存储第一个点的绝对坐标（16字节）
        data.resize(40);
        std::memcpy(&data[24], &coordinates[0].x, sizeof(double));
        std::memcpy(&data[32], &coordinates[0].y, sizeof(double));

        // 存储差分数据（使用变长整数编码）
        for (size_t i = 1; i < deltas.size(); ++i) {
            encodeVarint(data, deltas[i].first);
            encodeVarint(data, deltas[i].second);
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::decodeCoordinatesDelta(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 检查数据格式
        if (data.size() >= 40) {
            // 新格式：偏移量、分辨率、第一个点坐标、差分数据
            double min_x, min_y, scale;
            std::memcpy(&min_x, &data[0], sizeof(double));
            std::memcpy(&min_y, &data[8], sizeof(double));
            std::memcpy(&scale, &data[16], sizeof(double));

            // 读取第一个点的绝对坐标
            Coordinate first_point;
            std::memcpy(&first_point.x, &data[24], sizeof(double));
            std::memcpy(&first_point.y, &data[32], sizeof(double));
            coordinates.push_back(first_point);

            // 解码差分数据
            size_t offset = 40;
            while (offset < data.size()) {
                int64_t dx = decodeVarint(data, offset);
                int64_t dy = decodeVarint(data, offset);

                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx * scale, prev.y + dy * scale);
            }
        } else if (data.size() >= 16) {
            // 旧格式：直接存储绝对坐标
            Coordinate first_point;
            std::memcpy(&first_point.x, &data[0], sizeof(double));
            std::memcpy(&first_point.y, &data[8], sizeof(double));
            coordinates.push_back(first_point);

            // 解码后续点的差值
            size_t offset = 16;
            while (offset + 8 <= data.size()) {
                float dx, dy;
                std::memcpy(&dx, &data[offset], sizeof(float));
                offset += sizeof(float);
                std::memcpy(&dy, &data[offset], sizeof(float));
                offset += sizeof(float);

                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
            }
        }

        return coordinates;
    }

    BBox GeometrySerializer::calculateBBox(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return BBox();
        }

        double min_x = coordinates[0].x;
        double min_y = coordinates[0].y;
        double max_x = coordinates[0].x;
        double max_y = coordinates[0].y;

        for (const auto& coord : coordinates) {
            min_x = std::min(min_x, coord.x);
            min_y = std::min(min_y, coord.y);
            max_x = std::max(max_x, coord.x);
            max_y = std::max(max_y, coord.y);
        }

        return BBox(min_x, min_y, max_x, max_y);
    }

} // namespace GisStorage
