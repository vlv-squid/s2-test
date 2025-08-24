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
        """差分编码压缩坐标，与C++版本兼容"""
        if not coordinates:
            return b""

        if len(coordinates) == 1:
            # 单点情况，直接存储绝对坐标
            return struct.pack("dd", coordinates[0][0], coordinates[0][1])

        # 计算偏移量范围，决定使用哪种数据类型
        max_delta = 0.0
        deltas = []
        for i in range(1, len(coordinates)):
            dx = float(coordinates[i][0] - coordinates[i - 1][0])
            dy = float(coordinates[i][1] - coordinates[i - 1][1])
            deltas.append((dx, dy))
            max_delta = max(max_delta, abs(dx), abs(dy))

        # 存储第一个点的绝对坐标
        data = bytearray(struct.pack("dd", coordinates[0][0], coordinates[0][1]))

        # 对于小偏移量，使用float类型以保持精度
        if max_delta < 1.0:  # 小偏移量使用float
            data.append(2)  # 标记使用float类型
            for dx, dy in deltas:
                data.extend(struct.pack("ff", dx, dy))
        elif max_delta < 32767:  # short类型范围
            # 使用short类型存储偏移量（2字节/坐标）
            data.append(0)  # 标记使用short类型
            for dx, dy in deltas:
                data.extend(struct.pack("hh", int(round(dx)), int(round(dy))))
        elif max_delta < 2147483647:  # int类型范围
            # 使用int类型存储偏移量（4字节/坐标）
            data.append(1)  # 标记使用int类型
            for dx, dy in deltas:
                data.extend(struct.pack("ii", int(round(dx)), int(round(dy))))
        else:
            # 使用float类型存储偏移量（4字节/坐标）
            data.append(2)  # 标记使用float类型
            for dx, dy in deltas:
                data.extend(struct.pack("ff", dx, dy))

        return bytes(data)

    @staticmethod
    def decode_coordinates_delta(data: bytes) -> List[Tuple[float, float]]:
        """解码差分编码的坐标，与C++版本兼容"""
        if not data:
            return []

        if len(data) == 16:
            # 单点情况
            x, y = struct.unpack("dd", data)
            return [(x, y)]

        if len(data) < 17:
            return []

        coordinates = []

        # 读取第一个点的绝对坐标
        x, y = struct.unpack("dd", data[:16])
        coordinates.append((x, y))

        # 读取数据类型标记
        type_flag = data[16]
        pos = 17

        if type_flag == 0:  # short类型
            while pos + 4 <= len(data):
                dx, dy = struct.unpack("hh", data[pos : pos + 4])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 4
        elif type_flag == 1:  # int类型
            while pos + 8 <= len(data):
                dx, dy = struct.unpack("ii", data[pos : pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8
        elif type_flag == 2:  # float类型
            while pos + 8 <= len(data):
                dx, dy = struct.unpack("ff", data[pos : pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8

        return coordinates

    @staticmethod
    def serialize_geometry(geometry: GeometryData) -> bytes:
        """序列化整个几何对象，与C++版本兼容"""
        # 格式: feature_id(8) + geometry_type(1) + bbox(32) + coord_size(4) + coords
        data = struct.pack(
            "Qbdddd",
            geometry.feature_id,
            geometry.geometry_type.value,
            geometry.bbox[0],  # min_x
            geometry.bbox[1],  # min_y
            geometry.bbox[2],  # max_x
            geometry.bbox[3],  # max_y
        )

        # 写入坐标数据
        coord_size = len(geometry.coordinates)
        data += struct.pack("I", coord_size)
        data += geometry.coordinates
        return data

    @staticmethod
    def deserialize_geometry(data: bytes) -> Tuple[GeometryData, int]:
        """从二进制数据反序列化几何对象，与C++版本兼容"""
        if len(data) < 48:
            raise ValueError("数据长度不足，无法反序列化几何对象")

        # 解析基本字段
        feature_id, geometry_type_val, min_x, min_y, max_x, max_y = struct.unpack(
            "Qbdddd", data[:48]
        )

        offset = 48

        # 检查坐标数据长度
        if offset + 4 > len(data):
            raise ValueError("坐标数据长度不足")

        coord_size = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        if offset + coord_size > len(data):
            raise ValueError("坐标数据不完整")

        # 获取压缩的坐标数据
        compressed_coords = data[offset : offset + coord_size]

        return (
            GeometryData(
                feature_id,
                GeometryType(geometry_type_val),
                compressed_coords,
                (min_x, min_y, max_x, max_y),
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
