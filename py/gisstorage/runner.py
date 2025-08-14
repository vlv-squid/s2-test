# runner.py
"""
使用示例
"""

import sys

from pathlib import Path

# 获取当前脚本的父目录（即py目录）
project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

# 现在可以正确导入index模块
from index.s2_index import S2SpatialIndex
from gisstorage.gissystem import GisStorageSystem

if __name__ == "__main__":
    # 初始化GIS存储系统
    storage_system = GisStorageSystem("./output_data")

    # 1. 转换Shapefile
    print("=== 转换Shapefile ===")
    output_dir = storage_system.convert_shapefile("./data/test.shp",
                                                  s2_resolution=15)
    print(f"数据已保存至: {output_dir}")

    # # 2. 构建S2索引
    # print("\n=== 构建S2索引 ===")
    # s2_index = S2SpatialIndex("./data/test.shp",
    #                           "./index_py/s2.pkl",
    #                           resolution=15)
    # s2_index.build_index()

    # # 3. 查询要素
    # print("\n=== 空间查询 ===")
    # bbox = (100.0, 30.0, 105.0, 35.0)
    # features = storage_system.query_by_region(bbox, s2_index)
    # print(f"查询到 {len(features)} 个要素")

    # # 4. 获取属性信息
    # print("\n=== 获取属性信息 ===")
    # for feature in features[:5]:  # 只显示前5个要素
    #     attrs = storage_system.get_attributes(feature['id'])
    #     print(f"要素 {feature['id']} 属性: {attrs}")
    #     print(f"坐标: {feature['coordinates'][:3]}...")  # 只显示前3个坐标点
