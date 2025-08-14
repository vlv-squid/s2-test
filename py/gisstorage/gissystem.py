# gissystem.py
"""主程序入口"""

import os
from typing import Tuple, List, Dict
from gisstorage.converter import ShapefileConverter
from gisstorage.index_adapter import S2IndexAdapter
from gisstorage.serializers import GeometrySerializer
from gisstorage.storage import GeometryStorage, AttributeStorage


class GisStorageSystem:
    """GIS存储系统主类"""

    def __init__(self, data_dir: str):
        self.data_dir = data_dir
        self.geometry_file = os.path.join(data_dir, "geom.dat")
        self.attribute_file = os.path.join(data_dir, "attr.dat")

        # 初始化存储管理器
        self.geometry_storage = GeometryStorage(self.geometry_file)
        self.attribute_storage = AttributeStorage(self.attribute_file)

    def convert_shapefile(self,
                          shapefile_path: str,
                          s2_resolution: int = 15) -> str:
        """转换Shapefile"""
        converter = ShapefileConverter(self.data_dir)
        return converter.convert(shapefile_path, s2_resolution)

    def query_by_region(self, bbox: Tuple[float, float, float, float],
                        s2_index_instance) -> List[Dict]:
        """基于区域查询要素"""
        # 使用S2索引查找候选要素
        s2_adapter = S2IndexAdapter()
        candidate_fids = s2_adapter.query_by_bbox(s2_index_instance, bbox)

        results = []
        for fid in candidate_fids:
            try:
                # 读取几何数据
                geometry = self.geometry_storage.read_geometry(fid)

                # 获取坐标
                coords, _ = GeometrySerializer.deserialize_coordinates(
                    geometry.coordinates)

                # 构建结果
                result = {
                    'id': geometry.feature_id,
                    'type': geometry.geometry_type,
                    'coordinates': coords,
                    'bbox': geometry.bbox
                }
                results.append(result)
            except Exception as e:
                print(f"读取要素 {fid} 时出错: {e}")

        return results

    def get_attributes(self, feature_id: int) -> Dict:
        """获取指定要素的属性"""
        return self.attribute_storage.read_attribute(feature_id)
