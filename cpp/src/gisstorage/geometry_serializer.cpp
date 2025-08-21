//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_serializer.h"

#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace GisStorage {

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

        std::vector<uint8_t> data;
        data.reserve(16 + (coordinates.size() - 1) * 8); // 第一个点16字节，后续每点8字节

        // 第一个点存储绝对坐标
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].x), reinterpret_cast<const uint8_t*>(&coordinates[0].x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].y), reinterpret_cast<const uint8_t*>(&coordinates[0].y) + sizeof(double));

        // 后续点存储相对于前一个点的偏移量
        for (size_t i = 1; i < coordinates.size(); ++i) {
            float dx = static_cast<float>(coordinates[i].x - coordinates[i - 1].x);
            float dy = static_cast<float>(coordinates[i].y - coordinates[i - 1].y);

            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dx), reinterpret_cast<uint8_t*>(&dx) + sizeof(float));
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dy), reinterpret_cast<uint8_t*>(&dy) + sizeof(float));
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::decodeCoordinatesDelta(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取第一个点的绝对坐标
        if (data.size() >= 16) {
            Coordinate first_point;
            std::memcpy(&first_point.x, &data[0], sizeof(double));
            std::memcpy(&first_point.y, &data[8], sizeof(double));
            coordinates.push_back(first_point);

            // 读取后续点的偏移量
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
