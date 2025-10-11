# geometry_serializer.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import struct
from typing import List, Tuple
from .types import GeometryType, Coordinate, BBox
from .geometry_data import GeometryData


class GeometrySerializer:
    """几何数据序列化器，与C++版本兼容"""

    @staticmethod
    def serialize_geometry(geometry: GeometryData) -> bytes:
        """序列化几何数据，与C++版本完全一致"""
        data = bytearray()
        # Python的bytearray没有reserve方法，使用预分配
        data = bytearray(geometry.get_serialized_size())

        # 写入feature_id (8字节)
        feature_id = geometry.get_feature_id()
        data.extend(struct.pack("Q", feature_id))

        # 写入geometry_type (1字节)
        geom_type = geometry.get_geometry_type().value
        data.append(geom_type)

        # 写入7字节填充（与C++版本保持一致）
        data.extend(b"\x00" * 7)

        # 写入bbox (32字节)
        bbox = geometry.get_bbox()
        data.extend(struct.pack("dddd", bbox.min_x, bbox.min_y, bbox.max_x, bbox.max_y))

        # 对于多边形，写入环的数量 (4字节)
        if geometry.get_geometry_type() == GeometryType.POLYGON:
            num_rings = geometry.get_num_rings()
            data.extend(struct.pack("I", num_rings))
        else:
            # 对于非多边形，写入0表示只有一组坐标
            data.extend(struct.pack("I", 0))

        # 写入坐标数据大小 (4字节)
        coord_size = len(geometry.get_coordinates())
        data.extend(struct.pack("I", coord_size))

        # 写入坐标数据
        coords = geometry.get_coordinates()
        data.extend(coords)

        return bytes(data)

    @staticmethod
    def deserialize_geometry(data: bytes) -> GeometryData:
        """反序列化几何数据，与C++版本完全一致"""
        if len(data) < 56:  # 增加了4字节的环数量字段
            raise ValueError("数据长度不足，无法反序列化几何对象")

        offset = 0

        # 读取feature_id
        feature_id = struct.unpack("Q", data[offset : offset + 8])[0]
        offset += 8

        # 读取geometry_type
        geom_type_byte = data[offset]
        geometry_type = GeometryType(geom_type_byte)
        offset += 1

        # 跳过7字节填充（与C++版本保持一致）
        offset += 7

        # 读取bbox
        min_x, min_y, max_x, max_y = struct.unpack("dddd", data[offset : offset + 32])
        bbox = BBox(min_x, min_y, max_x, max_y)
        offset += 32

        # 读取环的数量
        num_rings = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        # 读取坐标数据大小
        coord_size = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        if offset + coord_size > len(data):
            raise ValueError("坐标数据不完整")

        # 读取坐标数据
        coordinates = data[offset : offset + coord_size]

        return GeometryData(feature_id, geometry_type, coordinates, bbox, num_rings)

    @staticmethod
    def serialize_coordinates(coordinates: List[Coordinate]) -> bytes:
        """序列化坐标数据"""
        data = bytearray()
        # Python的bytearray没有reserve方法，使用预分配
        data = bytearray(4 + len(coordinates) * 16)  # 4字节数量 + 每个坐标16字节

        # 写入点的数量
        num_points = len(coordinates)
        data.extend(struct.pack("I", num_points))

        # 写入所有坐标点
        for coord in coordinates:
            data.extend(struct.pack("dd", coord.x, coord.y))

        return bytes(data)

    @staticmethod
    def deserialize_coordinates(data: bytes) -> List[Coordinate]:
        """反序列化坐标数据"""
        if len(data) < 4:
            return []

        coordinates = []

        # 读取点的数量
        num_points = struct.unpack("I", data[:4])[0]

        offset = 4
        # Python的list没有reserve方法，使用预分配
        coordinates = [None] * num_points

        for i in range(num_points):
            if offset + 16 <= len(data):
                x, y = struct.unpack("dd", data[offset : offset + 16])
                coordinates.append(Coordinate(x, y))
                offset += 16

        return coordinates

    @staticmethod
    def encode_coordinates_delta(coordinates: List[Coordinate]) -> bytes:
        """差分编码坐标压缩，与C++版本完全一致"""
        if not coordinates:
            return b""

        if len(coordinates) == 1:
            # 单点情况，直接存储绝对坐标
            data = bytearray(16)
            struct.pack_into("dd", data, 0, coordinates[0].x, coordinates[0].y)
            return bytes(data)

        # FileGDB风格的整型化和压缩
        # 1. 计算数据的空间范围
        min_x = min_y = float("inf")
        max_x = max_y = float("-inf")

        for coord in coordinates:
            min_x = min(min_x, coord.x)
            max_x = max(max_x, coord.x)
            min_y = min(min_y, coord.y)
            max_y = max(max_y, coord.y)

        # 2. 计算分辨率，保持足够的精度
        if max_x == min_x and max_y == min_y:
            # 所有点都相同，使用固定分辨率
            scale = 1e-9
        else:
            # 计算合适的分辨率，确保整型化后不会溢出
            range_x = max_x - min_x
            range_y = max_y - min_y
            max_range = max(range_x, range_y)

            # 使用1e9作为整型化范围，确保精度
            scale = max(1e-9, max_range / 1e9)

        # 3. 整型化坐标并计算差分
        int_coords = []
        deltas = []

        for coord in coordinates:
            x_int = int((coord.x - min_x) / scale)
            y_int = int((coord.y - min_y) / scale)
            int_coords.append((x_int, y_int))

        # 计算差分（第一个点存储绝对值，后续点存储差值）
        deltas.append((int_coords[0][0], int_coords[0][1]))
        for i in range(1, len(int_coords)):
            dx = int_coords[i][0] - int_coords[i - 1][0]
            dy = int_coords[i][1] - int_coords[i - 1][1]
            deltas.append((dx, dy))

        # 4. 存储数据：偏移量、分辨率、第一个点坐标、差分数据
        data = bytearray()
        # Python的bytearray没有reserve方法，使用预分配
        data = bytearray(32 + len(deltas) * 4)  # 预估大小

        # 存储偏移量（16字节）
        data.extend(struct.pack("dd", min_x, min_y))

        # 存储分辨率（8字节）
        data.extend(struct.pack("d", scale))

        # 存储第一个点的绝对坐标（16字节）
        data.extend(struct.pack("dd", coordinates[0].x, coordinates[0].y))

        # 存储差分数据（使用变长整数编码）
        for i in range(1, len(deltas)):
            dx, dy = deltas[i]
            data.extend(GeometrySerializer._encode_varint(dx))
            data.extend(GeometrySerializer._encode_varint(dy))

        return bytes(data)

    @staticmethod
    def decode_coordinates_delta(data: bytes) -> List[Coordinate]:
        """解码差分编码坐标，与C++版本完全一致"""
        if not data:
            return []

        coordinates = []

        # 处理单点情况（16字节）
        if len(data) == 16:
            x, y = struct.unpack("dd", data)
            coordinates.append(Coordinate(x, y))
            return coordinates

        # FileGDB风格压缩格式：偏移量、分辨率、第一个点坐标、差分数据
        if len(data) >= 40:
            min_x, min_y = struct.unpack("dd", data[:16])
            scale = struct.unpack("d", data[16:24])[0]

            # 读取第一个点的绝对坐标
            first_x, first_y = struct.unpack("dd", data[24:40])
            coordinates.append(Coordinate(first_x, first_y))

            # 解码差分数据
            offset = 40
            while offset < len(data):
                dx, offset = GeometrySerializer._decode_varint(data, offset)
                dy, offset = GeometrySerializer._decode_varint(data, offset)

                prev = coordinates[-1]
                coordinates.append(Coordinate(prev.x + dx * scale, prev.y + dy * scale))

        return coordinates

    @staticmethod
    def serialize_multi_ring_polygon(rings: List[List[Coordinate]]) -> bytes:
        """多环多边形序列化，与C++版本完全一致"""
        data = bytearray()

        # 写入环的数量
        num_rings = len(rings)
        data.extend(struct.pack("I", num_rings))

        # 为每个环写入坐标数据
        for ring in rings:
            ring_data = GeometrySerializer.encode_coordinates_delta(ring)
            # 写入环数据大小
            ring_size = len(ring_data)
            data.extend(struct.pack("I", ring_size))
            # 写入环数据
            data.extend(ring_data)

        return bytes(data)

    @staticmethod
    def deserialize_multi_ring_polygon(data: bytes) -> List[List[Coordinate]]:
        """多环多边形反序列化，与C++版本完全一致"""
        rings = []

        if len(data) < 4:
            return rings

        offset = 0

        # 读取环的数量
        num_rings = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        # Python的list没有reserve方法，使用预分配
        rings = [None] * num_rings

        # 读取每个环的数据
        for i in range(num_rings):
            if offset + 4 <= len(data):
                # 读取环数据大小
                ring_size = struct.unpack("I", data[offset : offset + 4])[0]
                offset += 4

                if offset + ring_size <= len(data):
                    # 读取环数据
                    ring_data = data[offset : offset + ring_size]
                    coordinates = GeometrySerializer.decode_coordinates_delta(ring_data)
                    rings.append(coordinates)

                    offset += ring_size

        return rings

    @staticmethod
    def calculate_bbox(coordinates: List[Coordinate]) -> BBox:
        """计算边界框，与C++版本完全一致"""
        if not coordinates:
            return BBox()

        min_x = min_y = float("inf")
        max_x = max_y = float("-inf")

        for coord in coordinates:
            min_x = min(min_x, coord.x)
            min_y = min(min_y, coord.y)
            max_x = max(max_x, coord.x)
            max_y = max(max_y, coord.y)

        return BBox(min_x, min_y, max_x, max_y)

    @staticmethod
    def _encode_varint(value: int) -> bytes:
        """变长整数编码辅助函数（类似Protocol Buffers的varint）"""
        # 使用ZigZag编码处理负数
        zigzag = (value << 1) ^ (value >> 63)

        data = bytearray()
        while zigzag >= 0x80:
            data.append((zigzag & 0xFF) | 0x80)
            zigzag >>= 7
        data.append(zigzag & 0xFF)

        return bytes(data)

    @staticmethod
    def _decode_varint(data: bytes, offset: int) -> Tuple[int, int]:
        """变长整数解码辅助函数"""
        result = 0
        shift = 0

        while offset < len(data):
            byte = data[offset]
            offset += 1
            result |= (byte & 0x7F) << shift

            if (byte & 0x80) == 0:
                break
            shift += 7

        # ZigZag解码
        return ((result >> 1) ^ (-(result & 1)), offset)
