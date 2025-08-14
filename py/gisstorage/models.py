# models.py
"""数据模型定义"""

from typing import List, Tuple, Dict, Any


class GeometryData:
    """几何数据结构"""

    def __init__(self, feature_id: int, geometry_type: int, coordinates: bytes,
                 bbox: Tuple[float, float, float, float]):
        self.feature_id = feature_id
        self.geometry_type = geometry_type  # 0=Point, 1=Line, 2=Polygon等
        self.coordinates = coordinates
        self.bbox = bbox


class AttributeData:
    """属性数据结构"""

    def __init__(self, feature_id: int, properties: Dict[str, Any]):
        self.feature_id = feature_id
        self.properties = properties
