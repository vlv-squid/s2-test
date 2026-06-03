//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_serializer.h"

#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace GisStorage {

    static void encodeVarint(std::vector<uint8_t>& data, int64_t value) {
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

        return static_cast<int64_t>((result >> 1) ^ (-(result & 1)));
    }

    std::vector<uint8_t> GeometrySerializer::SerializeGeometry(const GeometryData& geometry) {
        std::vector<uint8_t> data;
        data.reserve(geometry.GetSerializedSize());
        uint64_t feature_id = geometry.GetFeatureId();
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&feature_id), reinterpret_cast<uint8_t*>(&feature_id) + sizeof(uint64_t));
        uint8_t geom_type = static_cast<uint8_t>(geometry.GetGeometryType());
        data.push_back(geom_type);
        std::vector<uint8_t> padding(7, 0);
        data.insert(data.end(), padding.begin(), padding.end());

        const BBox& bbox = geometry.GetBBox();
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_x), reinterpret_cast<const uint8_t*>(&bbox.min_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_y), reinterpret_cast<const uint8_t*>(&bbox.min_y) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_x), reinterpret_cast<const uint8_t*>(&bbox.max_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_y), reinterpret_cast<const uint8_t*>(&bbox.max_y) + sizeof(double));
        if (geometry.GetGeometryType() == GeometryType::POLYGON) {
            uint32_t num_rings = geometry.GetNumRings();
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_rings), reinterpret_cast<uint8_t*>(&num_rings) + sizeof(uint32_t));
        } else {
            uint32_t num_rings = 0;
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_rings), reinterpret_cast<uint8_t*>(&num_rings) + sizeof(uint32_t));
        }

        uint32_t coord_size = static_cast<uint32_t>(geometry.GetCoordinates().size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));
        const auto& coords = geometry.GetCoordinates();
        data.insert(data.end(), coords.begin(), coords.end());

        return data;
    }

    std::unique_ptr<GeometryData> GeometrySerializer::DeserializeGeometry(const std::vector<uint8_t>& data) {
        if (data.size() < 52) {
            throw std::runtime_error("数据长度不足，无法反序列化几何对象");
        }

        size_t offset = 0;
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);
        uint8_t geom_type_byte;
        std::memcpy(&geom_type_byte, &data[offset], sizeof(uint8_t));
        GeometryType geometry_type = static_cast<GeometryType>(geom_type_byte);
        offset += sizeof(uint8_t);
        offset += 7;

        BBox bbox;
        std::memcpy(&bbox.min_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.min_y, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_y, &data[offset], sizeof(double));
        offset += sizeof(double);

        uint32_t num_rings;
        std::memcpy(&num_rings, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        uint32_t coord_size;
        std::memcpy(&coord_size, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        if (offset + coord_size > data.size()) {
            throw std::runtime_error("坐标数据不完整");
        }
        std::vector<uint8_t> coordinates(data.begin() + offset, data.begin() + offset + coord_size);

        return std::make_unique<GeometryData>(feature_id, geometry_type, coordinates, bbox, num_rings);
    }

    std::vector<uint8_t> GeometrySerializer::SerializeCoordinates(const std::vector<Coordinate>& coordinates) {
        std::vector<uint8_t> data;
        data.reserve(4 + coordinates.size() * 16);

        uint32_t num_points = static_cast<uint32_t>(coordinates.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_points), reinterpret_cast<uint8_t*>(&num_points) + sizeof(uint32_t));
        for (const auto& coord : coordinates) {
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.x), reinterpret_cast<const uint8_t*>(&coord.x) + sizeof(double));
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.y), reinterpret_cast<const uint8_t*>(&coord.y) + sizeof(double));
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::DeserializeCoordinates(const std::vector<uint8_t>& data) {
        if (data.size() < 4) {
            return {};
        }

        std::vector<Coordinate> coordinates;
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

    std::vector<uint8_t> GeometrySerializer::EncodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        if (coordinates.size() == 1) {
            std::vector<uint8_t> data(16);
            std::memcpy(&data[0], &coordinates[0].x, sizeof(double));
            std::memcpy(&data[8], &coordinates[0].y, sizeof(double));
            return data;
        }

        double min_x = coordinates[0].x, max_x = coordinates[0].x;
        double min_y = coordinates[0].y, max_y = coordinates[0].y;
        for (const auto& coord : coordinates) {
            min_x = std::min(min_x, coord.x);
            max_x = std::max(max_x, coord.x);
            min_y = std::min(min_y, coord.y);
            max_y = std::max(max_y, coord.y);
        }
        double scale;
        if (max_x == min_x && max_y == min_y) {
            scale = 1e-9;
        } else {
            double range_x = max_x - min_x;
            double range_y = max_y - min_y;
            double max_range = std::max(range_x, range_y);
            scale = std::max(1e-9, max_range / 1e9);
        }
        std::vector<std::pair<int64_t, int64_t>> int_coords;
        std::vector<std::pair<int64_t, int64_t>> deltas;

        for (const auto& coord : coordinates) {
            int64_t x_int = static_cast<int64_t>((coord.x - min_x) / scale);
            int64_t y_int = static_cast<int64_t>((coord.y - min_y) / scale);
            int_coords.emplace_back(x_int, y_int);
        }
        deltas.emplace_back(int_coords[0].first, int_coords[0].second);
        for (size_t i = 1; i < int_coords.size(); ++i) {
            deltas.emplace_back(int_coords[i].first - int_coords[i - 1].first, int_coords[i].second - int_coords[i - 1].second);
        }

        std::vector<uint8_t> data;
        data.reserve(32 + deltas.size() * 4);
        data.resize(16);
        std::memcpy(&data[0], &min_x, sizeof(double));
        std::memcpy(&data[8], &min_y, sizeof(double));
        data.resize(24);
        std::memcpy(&data[16], &scale, sizeof(double));

        data.resize(40);
        std::memcpy(&data[24], &coordinates[0].x, sizeof(double));
        std::memcpy(&data[32], &coordinates[0].y, sizeof(double));
        for (size_t i = 1; i < deltas.size(); ++i) {
            encodeVarint(data, deltas[i].first);
            encodeVarint(data, deltas[i].second);
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::DecodeCoordinatesDelta(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return {};
        }

        std::vector<Coordinate> coordinates;
        if (data.size() == 16) {
            Coordinate point;
            std::memcpy(&point.x, &data[0], sizeof(double));
            std::memcpy(&point.y, &data[8], sizeof(double));
            coordinates.push_back(point);
            return coordinates;
        }
        if (data.size() >= 40) {
            double min_x, min_y, scale;
            std::memcpy(&min_x, &data[0], sizeof(double));
            std::memcpy(&min_y, &data[8], sizeof(double));
            std::memcpy(&scale, &data[16], sizeof(double));

            Coordinate first_point;
            std::memcpy(&first_point.x, &data[24], sizeof(double));
            std::memcpy(&first_point.y, &data[32], sizeof(double));
            coordinates.push_back(first_point);

            size_t offset = 40;
            while (offset < data.size()) {
                int64_t dx = decodeVarint(data, offset);
                int64_t dy = decodeVarint(data, offset);

                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx * scale, prev.y + dy * scale);
            }
        }

        return coordinates;
    }

    std::vector<uint8_t> GeometrySerializer::SerializeMultiRingPolygon(const std::vector<std::vector<Coordinate>>& rings) {
        std::vector<uint8_t> data;
        uint32_t num_rings = static_cast<uint32_t>(rings.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_rings), reinterpret_cast<uint8_t*>(&num_rings) + sizeof(uint32_t));
        for (const auto& ring : rings) {
            auto ring_data = EncodeCoordinatesDelta(ring);
            uint32_t ring_size = static_cast<uint32_t>(ring_data.size());
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&ring_size), reinterpret_cast<uint8_t*>(&ring_size) + sizeof(uint32_t));
            data.insert(data.end(), ring_data.begin(), ring_data.end());
        }

        return data;
    }

    std::vector<std::vector<Coordinate>> GeometrySerializer::DeserializeMultiRingPolygon(const std::vector<uint8_t>& data) {
        std::vector<std::vector<Coordinate>> rings;

        if (data.size() < 4) {
            return rings;
        }

        size_t offset = 0;
        uint32_t num_rings;
        std::memcpy(&num_rings, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        rings.reserve(num_rings);
        for (uint32_t i = 0; i < num_rings && offset + 4 <= data.size(); ++i) {
            uint32_t ring_size;
            std::memcpy(&ring_size, &data[offset], sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (offset + ring_size > data.size()) {
                break;
            }

            std::vector<uint8_t> ring_data(data.begin() + offset, data.begin() + offset + ring_size);
            auto coordinates = DecodeCoordinatesDelta(ring_data);
            rings.push_back(coordinates);

            offset += ring_size;
        }

        return rings;
    }

    BBox GeometrySerializer::CalculateBBox(const std::vector<Coordinate>& coordinates) {
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
