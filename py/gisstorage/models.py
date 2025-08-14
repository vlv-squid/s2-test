# models.py
"""数据模型定义"""

from typing import List, Tuple, Dict, Any


class GeometryData:
    """几何数据结构"""

    def __init__(self, feature_id: int, geometry_type: int, coordinates: bytes,
                 bbox: Tuple[float, float, float,
                             float], s2_cell_ids: List[int]):
        self.feature_id = feature_id
        self.geometry_type = geometry_type  # 0=Point, 1=Line, 2=Polygon等
        self.coordinates = coordinates
        self.bbox = bbox
        self.s2_cell_ids = s2_cell_ids  # 存储覆盖的S2单元格ID列表


class AttributeData:
    """属性数据结构"""

    def __init__(self, feature_id: int, properties: Dict[str, Any]):
        self.feature_id = feature_id
        self.properties = properties


class S2IndexEntry:
    """S2索引条目"""

    def __init__(self, cell_id: int, offset: int, size: int):
        self.cell_id = cell_id
        self.offset = offset
        self.size = size
