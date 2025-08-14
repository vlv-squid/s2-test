# gissystem.py
"""主程序入口"""

import os
from typing import Tuple, List, Dict
from gisstorage.converter import ShapefileConverter
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

    def get_attributes(self, feature_id: int) -> Dict:
        """获取指定要素的属性"""
        return self.attribute_storage.read_attribute(feature_id)
