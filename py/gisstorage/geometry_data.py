# geometry_data.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

from typing import List, Tuple
from .types import GeometryType, Coordinate, BBox


class GeometryData:
    """几何数据类，与C++版本兼容"""

    def __init__(
        self,
        feature_id: int,
        geometry_type: GeometryType,
        coordinates: bytes,
        bbox: BBox,
        num_rings: int = 0,
    ):
        self.feature_id = feature_id
        self.geometry_type = geometry_type
        self.coordinates = coordinates  # 压缩的坐标数据
        self.bbox = bbox
        self.num_rings = num_rings  # 多边形的环数量（外环+内环）

    def get_feature_id(self) -> int:
        """获取要素ID"""
        return self.feature_id

    def get_geometry_type(self) -> GeometryType:
        """获取几何类型"""
        return self.geometry_type

    def get_coordinates(self) -> bytes:
        """获取坐标数据"""
        return self.coordinates

    def get_bbox(self) -> BBox:
        """获取边界框"""
        return self.bbox

    def get_num_rings(self) -> int:
        """获取环数量"""
        return self.num_rings

    def decode_coordinates(self) -> List[Coordinate]:
        """解码压缩的坐标数据"""
        # 延迟导入避免循环依赖
        from .geometry_serializer import GeometrySerializer

        return GeometrySerializer.decode_coordinates_delta(self.coordinates)

    def decode_multi_ring_coordinates(self) -> List[List[Coordinate]]:
        """解码多环多边形数据"""
        # 延迟导入避免循环依赖
        from .geometry_serializer import GeometrySerializer

        return GeometrySerializer.deserialize_multi_ring_polygon(self.coordinates)

    def get_serialized_size(self) -> int:
        """计算序列化后的大小"""
        # feature_id(8) + geometry_type(1) + padding(7) + bbox(32) + num_rings(4) + coord_size(4) + coords
        return 8 + 1 + 7 + 32 + 4 + 4 + len(self.coordinates)
