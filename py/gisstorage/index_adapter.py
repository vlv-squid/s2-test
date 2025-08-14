# index_adapter.py
"""S2索引适配器"""

from typing import List, Tuple
from s2sphere import LatLng, LatLngRect, RegionCoverer


class S2IndexAdapter:
    """S2索引适配器，用于与已有的S2索引实现集成"""

    def __init__(self, resolution: int = 15):
        self.resolution = resolution

    def calculate_s2_cells(
            self, bbox: Tuple[float, float, float, float]) -> List[int]:
        """计算几何对象覆盖的S2单元格"""
        min_lon, min_lat, max_lon, max_lat = bbox

        # 构建查询区域的 S2 单元格
        p1 = LatLng.from_degrees(min_lat, min_lon)
        p2 = LatLng.from_degrees(max_lat, max_lon)
        query_rect = LatLngRect.from_point_pair(p1, p2)

        coverer = RegionCoverer()
        coverer.min_level = self.resolution
        coverer.max_level = self.resolution
        cell_ids = coverer.get_covering(query_rect)

        return [cell.id() for cell in cell_ids]

    def query_by_bbox(self, s2_index_instance,
                      bbox: Tuple[float, float, float, float]) -> List[int]:
        """使用已有S2索引实例查询要素"""
        return s2_index_instance.query_by_bbox(bbox)
