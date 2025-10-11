# types.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

from typing import NamedTuple
from enum import IntEnum


class GeometryType(IntEnum):
    """几何数据类型枚举，与C++版本保持一致"""

    POINT = 0
    LINE = 1
    POLYGON = 2
    MULTIPOINT = 3
    MULTILINE = 4
    MULTIPOLYGON = 5


class Coordinate(NamedTuple):
    """坐标点结构，与C++版本保持一致"""

    x: float
    y: float


class BBox(NamedTuple):
    """边界框结构，与C++版本保持一致"""

    min_x: float
    min_y: float
    max_x: float
    max_y: float

    def is_valid(self) -> bool:
        """检查边界框是否有效"""
        return self.min_x <= self.max_x and self.min_y <= self.max_y
