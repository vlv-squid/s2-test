# runner.py
"""
使用示例
"""

import sys
import os

from pathlib import Path

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from index.s2_index import S2SpatialIndex
from gisstorage.gissystem import GisStorageSystem

if __name__ == "__main__":
    # 1. 转换Shapefile
    print("=== 转换Shapefile ===")
    storage_system = GisStorageSystem("./output_data")
    validfid_num = storage_system.convert_shapefile("./data/test.shp")
    print(f"转换生成的有效FID数量: {len(validfid_num)}")

    # 2. 使用S2索引进行空间查询
    shapefile_name = os.path.splitext(os.path.basename("./data/test.shp"))[0]
    index_file = f"./index_py/s2.pkl"
    s2_index = S2SpatialIndex("./data/test.shp", index_file, resolution=15)

    print("\n=== 空间查询 ===")
    bbox = (103.2504, 26.4297, 103.3028, 26.4747)
    candidate_fids = s2_index.query_by_bbox(bbox)
    print(f"查询到 {len(candidate_fids)} 个候选要素")
