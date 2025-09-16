# models.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import struct
from typing import List, Tuple, Dict, Any, Optional
from enum import IntEnum


class GeometryType(IntEnum):
    """几何数据类型枚举，与C++版本保持一致"""

    POINT = 0
    LINE = 1
    POLYGON = 2
    MULTIPOINT = 3
    MULTILINE = 4
    MULTIPOLYGON = 5


class StringPool:
    """字符串池类 - 用于减少重复字符串的存储，与C++版本兼容"""

    def __init__(self):
        self.string_to_id = {}  # 字符串到ID的映射
        self.string_table = []  # ID到字符串的映射
        self.total_size = 0

    def get_string_id(self, string: str) -> int:
        """获取字符串ID（如果不存在则添加）"""
        if string in self.string_to_id:
            return self.string_to_id[string]

        # 新字符串，添加到池中
        new_id = len(self.string_table)
        self.string_to_id[string] = new_id
        self.string_table.append(string)
        self.total_size += self._calculate_string_size(string)

        return new_id

    def get_string(self, string_id: int) -> str:
        """根据ID获取字符串"""
        if 0 <= string_id < len(self.string_table):
            return self.string_table[string_id]
        return ""

    def serialize(self) -> bytes:
        """序列化字符串池"""
        data = bytearray()

        # 写入字符串数量
        count = len(self.string_table)
        data.extend(struct.pack("I", count))

        # 写入每个字符串
        for string in self.string_table:
            try:
                encoded_string = string.encode("utf-8")
                length = len(encoded_string)
                data.extend(struct.pack("I", length))
                data.extend(encoded_string)
            except UnicodeEncodeError as e:
                print(f"警告: 字符串编码失败 '{string}': {e}")
                # 跳过这个字符串
                continue

        return bytes(data)

    def deserialize(self, data: bytes) -> None:
        """反序列化字符串池"""
        self.clear()

        if len(data) < 4:
            return

        offset = 0

        # 读取字符串数量
        count = struct.unpack("I", data[offset : offset + 4])[0]
        offset += 4

        # 读取每个字符串
        for i in range(count):
            if offset + 4 > len(data):
                break

            length = struct.unpack("I", data[offset : offset + 4])[0]
            offset += 4

            if offset + length > len(data):
                break

            try:
                string = data[offset : offset + length].decode("utf-8")
                self.string_to_id[string] = i
                self.string_table.append(string)
                self.total_size += self._calculate_string_size(string)
            except UnicodeDecodeError as e:
                print(f"警告: 字符串解码失败 (位置 {offset}, 长度 {length}): {e}")
                # 跳过这个字符串，继续处理下一个
                pass

            offset += length

    def clear(self) -> None:
        """清空池"""
        self.string_to_id.clear()
        self.string_table.clear()
        self.total_size = 0

    def get_pool_size(self) -> int:
        """获取池中唯一字符串数量"""
        return len(self.string_table)

    def get_total_size(self) -> int:
        """获取总大小"""
        return self.total_size

    def _calculate_string_size(self, string: str) -> int:
        """计算字符串在池中的存储大小"""
        return len(string.encode("utf-8"))


class GeometryData:
    """几何数据结构，与C++版本兼容"""

    def __init__(
        self,
        feature_id: int,
        geometry_type: GeometryType,
        coordinates: bytes,
        bbox: Tuple[float, float, float, float],
        num_rings: int = 0,
    ):
        self.feature_id = feature_id
        self.geometry_type = geometry_type
        self.coordinates = coordinates  # 压缩的坐标数据
        self.bbox = bbox
        self.num_rings = num_rings  # 环数量（用于多边形）

    def decode_coordinates(self) -> List[Tuple[float, float]]:
        """解码压缩的坐标数据，与C++版本兼容"""
        if not self.coordinates:
            return []

        # 如果是单点情况
        if len(self.coordinates) == 16:
            x, y = struct.unpack("dd", self.coordinates)
            return [(x, y)]

        # 差分编码的情况
        if len(self.coordinates) < 17:
            return []

        coordinates = []

        # 读取第一个点的绝对坐标
        x, y = struct.unpack("dd", self.coordinates[:16])
        coordinates.append((x, y))

        if len(self.coordinates) <= 16:
            return coordinates

        # 读取数据类型标记
        type_flag = self.coordinates[16]
        pos = 17

        if type_flag == 0:  # short类型
            while pos + 4 <= len(self.coordinates):
                dx, dy = struct.unpack("hh", self.coordinates[pos : pos + 4])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 4
        elif type_flag == 1:  # int类型
            while pos + 8 <= len(self.coordinates):
                dx, dy = struct.unpack("ii", self.coordinates[pos : pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8
        elif type_flag == 2:  # float类型
            while pos + 8 <= len(self.coordinates):
                dx, dy = struct.unpack("ff", self.coordinates[pos : pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8

        return coordinates


class AttributeData:
    """属性数据结构，与C++版本兼容"""

    def __init__(self, feature_id: int, properties: Dict[str, str]):
        self.feature_id = feature_id
        self.properties = properties
