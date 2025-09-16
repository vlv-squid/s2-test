# serializers.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import struct
from typing import List, Tuple
from gisstorage.models import GeometryData, AttributeData, StringPool, GeometryType


class GeometrySerializer:
    """几何数据序列化器，与C++版本兼容"""

    @staticmethod
    def calculate_bbox(
        coordinates: List[Tuple[float, float]]
    ) -> Tuple[float, float, float, float]:
        """计算边界框，与C++版本兼容"""
        if not coordinates:
            return (0.0, 0.0, 0.0, 0.0)

        min_x = min_y = float("inf")
        max_x = max_y = float("-inf")

        for x, y in coordinates:
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)

        return (min_x, min_y, max_x, max_y)

    @staticmethod
    def encode_coordinates_delta(coordinates: List[Tuple[float, float]]) -> bytes:
        """FileGDB风格的差分编码压缩坐标，与C++版本兼容"""
        if not coordinates:
            return b""

        if len(coordinates) == 1:
            # 单点情况，直接存储绝对坐标
            return struct.pack("dd", coordinates[0][0], coordinates[0][1])

        # FileGDB风格的整型化和压缩
        # 1. 计算数据的空间范围
        min_x = min_y = float('inf')
        max_x = max_y = float('-inf')
        
        for x, y in coordinates:
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)

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
        
        for x, y in coordinates:
            x_int = int((x - min_x) / scale)
            y_int = int((y - min_y) / scale)
            int_coords.append((x_int, y_int))
        
        # 计算差分（第一个点存储绝对值，后续点存储差值）
        deltas.append((int_coords[0][0], int_coords[0][1]))
        for i in range(1, len(int_coords)):
            dx = int_coords[i][0] - int_coords[i-1][0]
            dy = int_coords[i][1] - int_coords[i-1][1]
            deltas.append((dx, dy))

        # 4. 存储数据：偏移量、分辨率、第一个点坐标、差分数据
        data = bytearray()
        
        # 存储偏移量（16字节）
        data.extend(struct.pack("dd", min_x, min_y))
        
        # 存储分辨率（8字节）
        data.extend(struct.pack("d", scale))
        
        # 存储第一个点的绝对坐标（16字节）
        data.extend(struct.pack("dd", coordinates[0][0], coordinates[0][1]))
        
        # 存储差分数据（使用变长整数编码）
        for i in range(1, len(deltas)):
            dx, dy = deltas[i]
            data.extend(GeometrySerializer._encode_varint(dx))
            data.extend(GeometrySerializer._encode_varint(dy))

        return bytes(data)

    @staticmethod
    def decode_coordinates_delta(data: bytes) -> List[Tuple[float, float]]:
        """解码FileGDB风格的差分编码坐标，与C++版本兼容"""
        if not data:
            return []

        if len(data) == 16:
            # 单点情况
            x, y = struct.unpack("dd", data)
            return [(x, y)]

        coordinates = []

        # FileGDB风格压缩格式：偏移量、分辨率、第一个点坐标、差分数据
        if len(data) >= 40:
            # 读取偏移量（16字节）
            min_x, min_y = struct.unpack("dd", data[:16])
            
            # 读取分辨率（8字节）
            scale = struct.unpack("d", data[16:24])[0]
            
            # 读取第一个点的绝对坐标（16字节）
            first_x, first_y = struct.unpack("dd", data[24:40])
            coordinates.append((first_x, first_y))
            
            # 解码差分数据
            offset = 40
            while offset < len(data):
                dx, offset = GeometrySerializer._decode_varint(data, offset)
                dy, offset = GeometrySerializer._decode_varint(data, offset)
                
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx * scale, prev_y + dy * scale))

        return coordinates

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

    @staticmethod
    def serialize_multi_ring_polygon(rings: List[List[Tuple[float, float]]]) -> bytes:
        """多环多边形序列化"""
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
    def deserialize_multi_ring_polygon(data: bytes) -> List[List[Tuple[float, float]]]:
        """多环多边形反序列化"""
        rings = []
        
        if len(data) < 4:
            return rings
        
        offset = 0
        
        # 读取环的数量
        num_rings = struct.unpack("I", data[offset:offset+4])[0]
        offset += 4
        
        # 读取每个环的数据
        for i in range(num_rings):
            if offset + 4 > len(data):
                break
            
            # 读取环数据大小
            ring_size = struct.unpack("I", data[offset:offset+4])[0]
            offset += 4
            
            if offset + ring_size > len(data):
                break
            
            # 读取环数据
            ring_data = data[offset:offset+ring_size]
            coordinates = GeometrySerializer.decode_coordinates_delta(ring_data)
            rings.append(coordinates)
            
            offset += ring_size
        
        return rings

    @staticmethod
    def serialize_geometry(geometry: GeometryData) -> bytes:
        """序列化整个几何对象，与C++版本兼容"""
        # 格式: feature_id(8) + geometry_type(1) + padding(7) + bbox(32) + num_rings(4) + coord_size(4) + coords
        data = bytearray()
        
        # 写入feature_id (8字节)
        data.extend(struct.pack("Q", geometry.feature_id))
        
        # 写入geometry_type (1字节)
        data.extend(struct.pack("B", geometry.geometry_type.value))
        
        # 写入7字节填充（与C++版本保持一致）
        data.extend(b'\x00' * 7)
        
        # 写入bbox (32字节)
        data.extend(struct.pack("dddd", 
            geometry.bbox[0],  # min_x
            geometry.bbox[1],  # min_y
            geometry.bbox[2],  # max_x
            geometry.bbox[3]   # max_y
        ))
        
        # 对于多边形，写入环的数量 (4字节)
        if geometry.geometry_type == GeometryType.POLYGON:
            # 这里需要从geometry对象获取环的数量，暂时使用1
            num_rings = getattr(geometry, 'num_rings', 1)
            data.extend(struct.pack("I", num_rings))
        else:
            # 对于非多边形，写入0表示只有一组坐标
            data.extend(struct.pack("I", 0))
        
        # 写入坐标数据大小 (4字节)
        coord_size = len(geometry.coordinates)
        data.extend(struct.pack("I", coord_size))
        
        # 写入坐标数据
        data.extend(geometry.coordinates)
        
        return bytes(data)

    @staticmethod
    def deserialize_geometry(data: bytes) -> Tuple[GeometryData, int]:
        """从二进制数据反序列化几何对象，使用新格式"""
        if len(data) < 56:  # 新格式最小长度：feature_id(8) + geometry_type(1) + padding(7) + bbox(32) + num_rings(4) + coord_size(4)
            raise ValueError("数据长度不足，无法反序列化几何对象")

        offset = 0

        # 读取feature_id
        feature_id = struct.unpack("Q", data[offset:offset+8])[0]
        offset += 8

        # 读取geometry_type
        geometry_type_val = struct.unpack("B", data[offset:offset+1])[0]
        offset += 1

        # 跳过7字节填充
        offset += 7

        # 读取bbox
        min_x, min_y, max_x, max_y = struct.unpack("dddd", data[offset:offset+32])
        offset += 32

        # 读取环的数量
        num_rings = struct.unpack("I", data[offset:offset+4])[0]
        offset += 4

        # 读取坐标数据大小
        coord_size = struct.unpack("I", data[offset:offset+4])[0]
        offset += 4

        if offset + coord_size > len(data):
            raise ValueError("坐标数据不完整")

        # 读取坐标数据
        compressed_coords = data[offset:offset+coord_size]

        return (
            GeometryData(
                feature_id,
                GeometryType(geometry_type_val),
                compressed_coords,
                (min_x, min_y, max_x, max_y),
                num_rings,
            ),
            offset + coord_size,
        )


class AttributeSerializer:
    """属性数据序列化器，使用字符串池，与C++版本兼容"""

    def __init__(self):
        self.string_pool = StringPool()
        self.stats = {
            "original_size": 0,
            "compressed_size": 0,
            "compression_ratio": 0.0,
            "unique_strings": 0,
            "total_strings": 0,
        }

    def serialize_attributes(self, attr: AttributeData) -> bytes:
        """序列化属性数据，使用字符串池"""
        data = bytearray()

        # 写入feature_id
        data.extend(struct.pack("Q", attr.feature_id))

        # 获取属性
        properties = attr.properties

        # 写入属性数量
        prop_count = len(properties)
        data.extend(struct.pack("I", prop_count))

        # 计算原始大小（用于统计）
        original_size = 8 + 4  # feature_id + prop_count

        # 序列化每个属性对
        for key, value in properties.items():
            # 获取字符串ID（如果不存在则添加到池中）
            key_id = self.string_pool.get_string_id(key)
            value_id = self.string_pool.get_string_id(value)

            # 写入key_id和value_id
            data.extend(struct.pack("II", key_id, value_id))

            # 计算原始大小
            original_size += len(key) + len(value) + 2  # 字符串长度 + 引号

        # 更新统计信息
        self._update_stats(original_size, len(data))

        return bytes(data)

    def deserialize_attributes(self, data: bytes) -> Tuple[AttributeData, int]:
        """反序列化属性数据，从字符串池恢复"""
        if len(data) < 12:  # 8 + 4
            raise ValueError("属性数据长度不足")

        offset = 0

        # 读取feature_id
        feature_id = struct.unpack("Q", data[offset : offset + 8])[0]
        offset += 8

        # 读取属性数量
        prop_count = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        # 重建属性映射
        properties = {}
        for i in range(prop_count):
            if offset + 8 > len(data):
                raise ValueError("属性数据不完整")

            # 读取key_id和value_id
            key_id, value_id = struct.unpack("II", data[offset : offset + 8])
            offset += 8

            # 从字符串池中获取字符串
            key = self.string_pool.get_string(key_id)
            value = self.string_pool.get_string(value_id)

            if not key or not value:
                raise ValueError("字符串池中找不到对应的字符串")

            properties[key] = value

        return AttributeData(feature_id, properties), offset

    def serialize_string_pool(self) -> bytes:
        """序列化字符串池"""
        return self.string_pool.serialize()

    def deserialize_string_pool(self, data: bytes) -> None:
        """反序列化字符串池"""
        self.string_pool.deserialize(data)

    def get_compression_stats(self) -> dict:
        """获取压缩统计信息"""
        self.stats["unique_strings"] = self.string_pool.get_pool_size()
        self.stats["total_strings"] = self.string_pool.get_total_size()
        return self.stats.copy()

    def get_pool_size(self) -> int:
        """获取字符串池大小"""
        return self.string_pool.get_pool_size()

    def get_pool_total_size(self) -> int:
        """获取字符串池总大小"""
        return self.string_pool.get_total_size()

    def clear_string_pool(self) -> None:
        """清空字符串池"""
        self.string_pool.clear()

    def _update_stats(self, original_size: int, compressed_size: int) -> None:
        """更新统计信息"""
        self.stats["original_size"] += original_size
        self.stats["compressed_size"] += compressed_size

        if self.stats["original_size"] > 0:
            self.stats["compression_ratio"] = (
                1.0 - self.stats["compressed_size"] / self.stats["original_size"]
            ) * 100.0
