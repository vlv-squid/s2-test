# performance_comparison.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import sys
import os
import time
import threading
from pathlib import Path
from typing import List, Tuple, Dict
import statistics

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from osgeo import ogr, osr
from gisstorage.gissystem import GisStorageSystem
from gisstorage.storage import GeometryStorage, AttributeStorage
from index.s2_index import S2SpatialIndex


class PerformanceComparison:
    """性能对比测试类"""

    def __init__(self, shapefile_path: str, output_dir: str = "./output_data"):
        self.shapefile_path = shapefile_path
        self.output_dir = output_dir
        self.shapefile_name = os.path.splitext(os.path.basename(shapefile_path))[0]

        # 初始化OGR数据源
        ogr.RegisterAll()
        self.ogr_datasource = ogr.Open(shapefile_path)
        self.ogr_layer = self.ogr_datasource.GetLayer(0)

        # 初始化自定义GIS存储系统
        self.gis_system = GisStorageSystem(output_dir)
        self.geometry_storage = GeometryStorage(
            f"{output_dir}/{self.shapefile_name}_geom.dat"
        )
        self.attribute_storage = AttributeStorage(
            f"{output_dir}/{self.shapefile_name}_attr.dat"
        )

        # 初始化S2索引
        self.s2_index = S2SpatialIndex(
            shapefile_path, f"./index_py/s2.pkl", resolution=15
        )

        # 测试用的查询范围
        self.test_bboxes = [(103.2504, 26.4297, 103.3028, 26.4747)]

    def convert_shapefile_if_needed(self):
        """如果需要，转换Shapefile为自定义格式"""
        geom_file = f"{self.output_dir}/{self.shapefile_name}_geom.dat"
        if not os.path.exists(geom_file):
            print("转换Shapefile为自定义格式...")
            valid_fids = self.gis_system.convert_shapefile(self.shapefile_path)
            print(f"转换完成，有效要素数量: {len(valid_fids)}")
        else:
            print("自定义格式文件已存在，跳过转换")

    def test_ogr_sequential_read(self, test_count: int = 1000) -> Dict:
        """测试OGR顺序读取性能"""
        print(f"\n=== OGR顺序读取测试 ({test_count}个要素) ===")

        self.ogr_layer.ResetReading()
        feature_count = 0
        start_time = time.time()

        # 读取几何数据
        geom_start = time.time()
        geometries = []
        for i, feature in enumerate(self.ogr_layer):
            if i >= test_count:
                break
            geom = feature.GetGeometryRef()
            if geom:
                geometries.append(geom.Clone())
            feature_count += 1
        geom_time = time.time() - geom_start

        # 读取属性数据
        attr_start = time.time()
        self.ogr_layer.ResetReading()
        attributes = []
        for i, feature in enumerate(self.ogr_layer):
            if i >= test_count:
                break
            attrs = {}
            for j in range(feature.GetFieldCount()):
                field_def = feature.GetFieldDefnRef(j)
                field_name = field_def.GetName()
                field_value = feature.GetField(j)
                attrs[field_name] = field_value
            attributes.append(attrs)
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": feature_count / geom_time if geom_time > 0 else 0,
            "attr_rate": feature_count / attr_time if attr_time > 0 else 0,
            "total_rate": feature_count / total_time if total_time > 0 else 0,
        }

    def test_custom_sequential_read(self, test_count: int = 1000) -> Dict:
        """测试自定义格式顺序读取性能"""
        print(f"\n=== 自定义格式顺序读取测试 ({test_count}个要素) ===")

        all_fids = self.geometry_storage.get_all_feature_ids()
        if not all_fids:
            print("没有找到要素数据")
            return {}

        test_fids = all_fids[:test_count]
        feature_count = len(test_fids)

        start_time = time.time()

        # 读取几何数据
        geom_start = time.time()
        geometries = []
        for fid in test_fids:
            try:
                geom_data = self.geometry_storage.read_geometry(fid)
                geometries.append(geom_data)
            except Exception as e:
                print(f"读取几何数据失败 FID {fid}: {e}")
        geom_time = time.time() - geom_start

        # 读取属性数据
        attr_start = time.time()
        attributes = []
        for fid in test_fids:
            try:
                attr_data = self.attribute_storage.read_attribute(fid)
                attributes.append(attr_data.properties if attr_data else {})
            except Exception as e:
                print(f"读取属性数据失败 FID {fid}: {e}")
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": feature_count / geom_time if geom_time > 0 else 0,
            "attr_rate": feature_count / attr_time if attr_time > 0 else 0,
            "total_rate": feature_count / total_time if total_time > 0 else 0,
        }

    def test_ogr_random_read(self, test_count: int = 1000) -> Dict:
        """测试OGR随机读取性能"""
        print(f"\n=== OGR随机读取测试 ({test_count}个要素) ===")

        # 获取所有要素ID
        self.ogr_layer.ResetReading()
        all_fids = []
        for feature in self.ogr_layer:
            all_fids.append(feature.GetFID())

        if len(all_fids) < test_count:
            test_count = len(all_fids)

        import random

        test_fids = random.sample(all_fids, test_count)

        start_time = time.time()

        # 随机读取几何数据
        geom_start = time.time()
        geometries = []
        for fid in test_fids:
            # 使用GetFeature通过FID直接获取要素
            feature = self.ogr_layer.GetFeature(fid)
            if feature:
                geom = feature.GetGeometryRef()
                if geom:
                    geometries.append(geom.Clone())
        geom_time = time.time() - geom_start

        # 随机读取属性数据
        attr_start = time.time()
        attributes = []
        for fid in test_fids:
            # 使用GetFeature通过FID直接获取要素
            feature = self.ogr_layer.GetFeature(fid)
            if feature:
                attrs = {}
                for j in range(feature.GetFieldCount()):
                    field_def = feature.GetFieldDefnRef(j)
                    field_name = field_def.GetName()
                    field_value = feature.GetField(j)
                    attrs[field_name] = field_value
                attributes.append(attrs)
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        return {
            "feature_count": test_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": test_count / geom_time if geom_time > 0 else 0,
            "attr_rate": test_count / attr_time if attr_time > 0 else 0,
            "total_rate": test_count / total_time if total_time > 0 else 0,
        }

    def test_custom_random_read(self, test_count: int = 1000) -> Dict:
        """测试自定义格式随机读取性能"""
        print(f"\n=== 自定义格式随机读取测试 ({test_count}个要素) ===")

        all_fids = self.geometry_storage.get_all_feature_ids()
        if not all_fids:
            print("没有找到要素数据")
            return {}

        if len(all_fids) < test_count:
            test_count = len(all_fids)

        import random

        test_fids = random.sample(all_fids, test_count)

        start_time = time.time()

        # 随机读取几何数据
        geom_start = time.time()
        geometries = []
        for fid in test_fids:
            try:
                geom_data = self.geometry_storage.read_geometry(fid)
                geometries.append(geom_data)
            except Exception as e:
                print(f"读取几何数据失败 FID {fid}: {e}")
        geom_time = time.time() - geom_start

        # 随机读取属性数据
        attr_start = time.time()
        attributes = []
        for fid in test_fids:
            try:
                attr_data = self.attribute_storage.read_attribute(fid)
                attributes.append(attr_data.properties if attr_data else {})
            except Exception as e:
                print(f"读取属性数据失败 FID {fid}: {e}")
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        return {
            "feature_count": test_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": test_count / geom_time if geom_time > 0 else 0,
            "attr_rate": test_count / attr_time if attr_time > 0 else 0,
            "total_rate": test_count / total_time if total_time > 0 else 0,
        }

    def test_ogr_spatial_query(self, bbox: Tuple[float, float, float, float]) -> Dict:
        """测试OGR空间查询性能"""
        print(f"\n=== OGR空间查询测试 (bbox: {bbox}) ===")

        start_time = time.time()

        # 设置空间过滤器
        self.ogr_layer.SetSpatialFilterRect(*bbox)
        self.ogr_layer.ResetReading()

        # 执行查询
        results = []
        while True:
            feature = self.ogr_layer.GetNextFeature()
            if not feature:
                break
            results.append(feature.GetFID())

        query_time = time.time() - start_time

        # 清除空间过滤器
        self.ogr_layer.SetSpatialFilter(None)

        return {
            "bbox": bbox,
            "result_count": len(results),
            "query_time": query_time,
            "query_rate": len(results) / query_time if query_time > 0 else 0,
        }

    def test_custom_spatial_query(
        self, bbox: Tuple[float, float, float, float]
    ) -> Dict:
        """测试自定义格式空间查询性能"""
        print(f"\n=== 自定义格式空间查询测试 (bbox: {bbox}) ===")

        start_time = time.time()

        # 使用S2索引进行查询
        candidate_fids = self.s2_index.query_by_bbox(bbox)

        # 精确验证（可选）
        results = []
        for fid in candidate_fids:
            try:
                geom_data = self.geometry_storage.read_geometry(fid)
                # 简单的边界框相交检查
                geom_bbox = geom_data.bbox
                if not (
                    geom_bbox[2] < bbox[0]
                    or geom_bbox[0] > bbox[2]
                    or geom_bbox[3] < bbox[1]
                    or geom_bbox[1] > bbox[3]
                ):
                    results.append(fid)
            except:
                pass

        query_time = time.time() - start_time

        return {
            "bbox": bbox,
            "candidate_count": len(candidate_fids),
            "result_count": len(results),
            "query_time": query_time,
            "query_rate": len(results) / query_time if query_time > 0 else 0,
        }

    def run_comprehensive_test(self, test_sizes: List[int] = [100, 1000, 10000]):
        """运行综合性能测试"""
        print("=" * 60)
        print("GIS存储格式性能对比测试")
        print("=" * 60)

        # 确保数据已转换
        self.convert_shapefile_if_needed()

        # 1. 顺序读取测试
        print("\n" + "=" * 40)
        print("1. 顺序读取性能测试")
        print("=" * 40)

        for size in test_sizes:
            print(f"\n--- 测试规模: {size}个要素 ---")

            # OGR测试
            ogr_result = self.test_ogr_sequential_read(size)
            if ogr_result:
                print(
                    f"OGR - 几何: {ogr_result['geom_rate']:.1f} 要素/秒, "
                    f"属性: {ogr_result['attr_rate']:.1f} 要素/秒, "
                    f"总计: {ogr_result['total_rate']:.1f} 要素/秒"
                )

            # 自定义格式测试
            custom_result = self.test_custom_sequential_read(size)
            if custom_result:
                print(
                    f"自定义 - 几何: {custom_result['geom_rate']:.1f} 要素/秒, "
                    f"属性: {custom_result['attr_rate']:.1f} 要素/秒, "
                    f"总计: {custom_result['total_rate']:.1f} 要素/秒"
                )

        # 2. 随机读取测试
        print("\n" + "=" * 40)
        print("2. 随机读取性能测试")
        print("=" * 40)

        for size in test_sizes:
            print(f"\n--- 测试规模: {size}个要素 ---")

            # OGR测试
            ogr_result = self.test_ogr_random_read(size)
            if ogr_result:
                print(
                    f"OGR - 几何: {ogr_result['geom_rate']:.1f} 要素/秒, "
                    f"属性: {ogr_result['attr_rate']:.1f} 要素/秒, "
                    f"总计: {ogr_result['total_rate']:.1f} 要素/秒"
                )

            # 自定义格式测试
            custom_result = self.test_custom_random_read(size)
            if custom_result:
                print(
                    f"自定义 - 几何: {custom_result['geom_rate']:.1f} 要素/秒, "
                    f"属性: {custom_result['attr_rate']:.1f} 要素/秒, "
                    f"总计: {custom_result['total_rate']:.1f} 要素/秒"
                )

        # 3. 空间查询测试
        print("\n" + "=" * 40)
        print("3. 空间查询性能测试")
        print("=" * 40)

        for i, bbox in enumerate(self.test_bboxes):
            print(f"\n--- 查询范围 {i+1}: {bbox} ---")

            # OGR测试
            ogr_result = self.test_ogr_spatial_query(bbox)
            print(
                f"OGR - 结果: {ogr_result['result_count']}个要素, "
                f"耗时: {ogr_result['query_time']*1000:.2f}ms, "
                f"速率: {ogr_result['query_rate']:.1f} 要素/秒"
            )

            # 自定义格式测试
            custom_result = self.test_custom_spatial_query(bbox)
            print(
                f"自定义 - 候选: {custom_result['candidate_count']}个, "
                f"结果: {custom_result['result_count']}个要素, "
                f"耗时: {custom_result['query_time']*1000:.2f}ms, "
                f"速率: {custom_result['query_rate']:.1f} 要素/秒"
            )

        print("\n" + "=" * 60)
        print("测试完成")
        print("=" * 60)


def main():
    """主函数"""
    shapefile_path = "./data/test.shp"

    if not os.path.exists(shapefile_path):
        print(f"错误: 找不到Shapefile文件 {shapefile_path}")
        print("请确保数据文件存在")
        return

    # 创建性能对比测试对象
    comparison = PerformanceComparison(shapefile_path)

    # 运行综合测试
    comparison.run_comprehensive_test()


if __name__ == "__main__":
    main()
