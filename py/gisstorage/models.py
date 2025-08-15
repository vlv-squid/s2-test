# models.py
"""数据模型定义"""

from typing import List, Tuple, Dict, Any


import struct
from typing import List, Tuple, Dict, Any


class GeometryData:
    """几何数据结构"""

    def __init__(self, feature_id: int, geometry_type: int, coordinates: bytes,
                 bbox: Tuple[float, float, float, float]):
        self.feature_id = feature_id
        self.geometry_type = geometry_type  # 0=Point, 1=Line, 2=Polygon等
        self.coordinates = coordinates  # 压缩的坐标数据
        self.bbox = bbox

    def decode_coordinates(self) -> List[Tuple[float, float]]:
        """解码压缩的坐标数据"""
        if not self.coordinates:
            return []

        # 如果是单点情况
        if len(self.coordinates) == 16:
            x, y = struct.unpack('dd', self.coordinates)
            return [(x, y)]

        # 差分编码的情况
        if len(self.coordinates) < 17:
            return []

        coordinates = []

        # 读取第一个点的绝对坐标
        x, y = struct.unpack('dd', self.coordinates[:16])
        coordinates.append((x, y))

        if len(self.coordinates) <= 16:
            return coordinates

        # 读取数据类型标记
        type_flag = self.coordinates[16]
        pos = 17

        if type_flag == 0:  # short类型
            while pos + 4 <= len(self.coordinates):
                dx, dy = struct.unpack('hh', self.coordinates[pos:pos + 4])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 4
        elif type_flag == 1:  # int类型
            while pos + 8 <= len(self.coordinates):
                dx, dy = struct.unpack('ii', self.coordinates[pos:pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8
        elif type_flag == 2:  # float类型
            while pos + 8 <= len(self.coordinates):
                dx, dy = struct.unpack('ff', self.coordinates[pos:pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8

        return coordinates


class AttributeData:
    """属性数据结构"""

    def __init__(self, feature_id: int, properties: Dict[str, Any]):
        self.feature_id = feature_id
        self.properties = properties
