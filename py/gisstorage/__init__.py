# gisstorage/__init__.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

from .geometry_types import GeometryType, Coordinate, BBox
from .geometry_data import GeometryData
from .attribute_data import AttributeData
from .geometry_serializer import GeometrySerializer
from .attribute_serializer import AttributeSerializer
from .string_pool import StringPool
from .geometry_storage import GeometryStorage
from .attribute_storage import AttributeStorage
from .gis_storage_system import GisStorageSystem
from .ogr_format_converter import OGRFormatConverter
from .runner import GisStorageRunner

__all__ = [
    # 新的模块结构（与C++版本一致）
    "GeometryType",
    "Coordinate",
    "BBox",
    "GeometryData",
    "AttributeData",
    "GeometrySerializer",
    "AttributeSerializer",
    "StringPool",
    "GeometryStorage",
    "AttributeStorage",
    "GisStorageSystem",
    "OGRFormatConverter",
    "GisStorageRunner",
]
