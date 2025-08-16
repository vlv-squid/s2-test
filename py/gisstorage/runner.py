# runner.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import sys
import os

from pathlib import Path

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from index.s2_index import S2SpatialIndex
from gisstorage.gissystem import GisStorageSystem
from gisstorage.storage import GeometryStorage, AttributeStorage

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

    # 3. 解析二进制文件测试
    print("\n=== 解析二进制文件测试 ===")

    # 初始化存储对象
    geometry_storage = GeometryStorage("./output_data/test_geom.dat")
    attribute_storage = AttributeStorage("./output_data/test_attr.dat")

    # 获取所有要素ID
    all_fids = geometry_storage.get_all_feature_ids()
    print(f"二进制文件中包含 {len(all_fids)} 个要素")

    if all_fids:
        print(f"要素ID列表: {all_fids[:10]}{'...' if len(all_fids) > 10 else ''}")

        # 测试解析前几个要素
        test_count = min(5, len(all_fids))
        print(f"\n测试解析前 {test_count} 个要素:")

        for i, fid in enumerate(all_fids[:test_count]):
            print(f"\n--- 要素 {i+1}: FID {fid} ---")

            try:
                # 读取几何数据
                geometry_data = geometry_storage.read_geometry(fid)
                decoded_coords = geometry_data.decode_coordinates()
                print(f"  几何类型: {geometry_data.geometry_type}")
                print(f"  边界框: {geometry_data.bbox}")
                print(f"  坐标数量: {len(decoded_coords)}")
                print(f"  坐标: {decoded_coords}")

                # 读取属性数据
                attribute_data = attribute_storage.read_attribute(fid)
                if attribute_data and attribute_data.properties:
                    print(f"  属性数量: {len(attribute_data.properties)}")
                    print(
                        f"  属性示例: {dict(list(attribute_data.properties.items())[:3])}"
                    )
                else:
                    print("  无属性数据")

            except Exception as e:
                print(f"  解析失败: {str(e)}")

    # 4. 性能测试
    print("\n=== 性能测试 ===")

    if all_fids:
        import time

        # 测试几何数据读取性能
        start_time = time.time()
        success_count = 0
        for fid in all_fids[:100]:  # 测试前100个要素
            try:
                geometry_data = geometry_storage.read_geometry(fid)
                success_count += 1
            except:
                pass
        geom_time = time.time() - start_time
        print(
            f"几何数据读取性能: {success_count} 个要素/{geom_time:.3f}秒 = {success_count/geom_time:.1f} 要素/秒"
        )

        # 测试属性数据读取性能
        start_time = time.time()
        success_count = 0
        for fid in all_fids[:100]:  # 测试前100个要素
            try:
                attribute_data = attribute_storage.read_attribute(fid)
                if attribute_data:
                    success_count += 1
            except:
                pass
        attr_time = time.time() - start_time
        print(
            f"属性数据读取性能: {success_count} 个要素/{attr_time:.3f}秒 = {success_count/attr_time:.1f} 要素/秒"
        )

    # 5. 数据完整性验证
    print("\n=== 数据完整性验证 ===")

    if all_fids:
        valid_geometry = 0
        valid_attribute = 0

        for fid in all_fids:
            try:
                geometry_data = geometry_storage.read_geometry(fid)
                if geometry_data and geometry_data.coordinates:
                    valid_geometry += 1
            except:
                pass

            try:
                attribute_data = attribute_storage.read_attribute(fid)
                if attribute_data and attribute_data.properties:
                    valid_attribute += 1
            except:
                pass

        print(
            f"有效几何数据: {valid_geometry}/{len(all_fids)} ({valid_geometry/len(all_fids)*100:.1f}%)"
        )
        print(
            f"有效属性数据: {valid_attribute}/{len(all_fids)} ({valid_attribute/len(all_fids)*100:.1f}%)"
        )

    print("\n=== 测试完成 ===")
