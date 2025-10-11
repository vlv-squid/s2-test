# benchmark.py
# 统一的GIS存储格式性能测试工具
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import sys
import os
import time
import json
import statistics
import argparse
from pathlib import Path
from typing import List, Tuple, Dict, Optional
import matplotlib.pyplot as plt
import numpy as np

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from osgeo import ogr
from gisstorage.gis_storage_system import GisStorageSystem
from gisstorage.geometry_storage import GeometryStorage
from gisstorage.attribute_storage import AttributeStorage
from gisstorage.ogr_format_converter import OGRFormatConverter
from gisindex.s2_index import S2SpatialIndex


class GISBenchmark:
    """统一的GIS存储格式性能测试类"""

    def __init__(self, shapefile_path: str, output_dir: str = "./output_data"):
        self.shapefile_path = shapefile_path
        self.output_dir = output_dir
        self.shapefile_name = os.path.splitext(os.path.basename(shapefile_path))[0]
        self.results_dir = f"{output_dir}/benchmark_results"
        os.makedirs(self.results_dir, exist_ok=True)

        # 初始化OGR数据源
        ogr.RegisterAll()
        self.ogr_datasource = ogr.Open(shapefile_path)
        self.ogr_layer = self.ogr_datasource.GetLayer(0)

        # 初始化自定义GIS存储系统
        self.gis_system = GisStorageSystem(output_dir, self.shapefile_name)
        self.geometry_storage = GeometryStorage(
            f"{output_dir}/{self.shapefile_name}.geom"
        )
        self.attribute_storage = AttributeStorage(
            f"{output_dir}/{self.shapefile_name}.attr",
            f"{output_dir}/{self.shapefile_name}.pool",
        )

        # 初始化S2索引
        self.s2_index = S2SpatialIndex(
            shapefile_path, "./index_py/s2.pkl", resolution=15
        )
        # 如果索引不存在，构建索引
        if not os.path.exists("./index_py/s2.pkl"):
            print("构建S2索引...")
            self.s2_index.build_index()

        # 测试配置
        self.test_sizes = [100, 500, 1000, 5000, 10000]
        self.test_iterations = 5
        self.test_bboxes = [
            (103.2504, 26.4297, 103.3028, 26.4747),  # 小范围
            (103.0, 26.0, 104.0, 27.0),  # 中等范围
            (102.0, 25.0, 105.0, 28.0),  # 大范围
        ]

    def convert_shapefile_if_needed(self):
        """如果需要，转换Shapefile为自定义格式"""
        geom_file = f"{self.output_dir}/{self.shapefile_name}.geom"
        if not os.path.exists(geom_file):
            print("转换Shapefile为自定义格式...")
            converter = OGRFormatConverter(self.shapefile_path, self.output_dir)
            success = converter.convert_shapefile_to_custom_format()
            if success:
                print("转换完成")
                # 转换完成后，重新加载字符串池
                if os.path.exists(f"{self.output_dir}/{self.shapefile_name}.pool"):
                    self.attribute_storage.load_string_pool()
            else:
                print("转换失败")
        else:
            print("自定义格式文件已存在，跳过转换")
            # 确保字符串池已加载
            if os.path.exists(f"{self.output_dir}/{self.shapefile_name}.pool"):
                self.attribute_storage.load_string_pool()

    def run_multiple_tests(self, test_func, *args, **kwargs) -> List[Dict]:
        """运行多次测试并返回结果列表"""
        results = []
        for i in range(self.test_iterations):
            print(f"  测试 {i+1}/{self.test_iterations}...")
            try:
                result = test_func(*args, **kwargs)
                if result:
                    results.append(result)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"    测试失败: {e}")
        return results

    def calculate_statistics(self, results: List[Dict], metric: str) -> Dict:
        """计算统计信息"""
        if not results:
            return {}

        values = [r.get(metric, 0) for r in results if r.get(metric, 0) > 0]
        if not values:
            return {}

        return {
            "count": len(values),
            "mean": statistics.mean(values),
            "median": statistics.median(values),
            "std": statistics.stdev(values) if len(values) > 1 else 0,
            "min": min(values),
            "max": max(values),
            "values": values,
        }

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

        # 计算性能指标
        geom_rate = feature_count / geom_time if geom_time > 0 else 0
        attr_rate = feature_count / attr_time if attr_time > 0 else 0
        total_rate = feature_count / total_time if total_time > 0 else 0

        print(
            f"OGR - 几何: {geom_rate:.1f} 要素/秒, 属性: {attr_rate:.1f} 要素/秒, 总计: {total_rate:.1f} 要素/秒"
        )

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": geom_rate,
            "attr_rate": attr_rate,
            "total_rate": total_rate,
        }

    def test_custom_sequential_read(self, test_count: int = 1000) -> Dict:
        """测试自定义格式顺序读取性能"""
        print(f"\n=== 自定义格式顺序读取测试 ({test_count}个要素) ===")

        # 获取所有要素ID
        all_fids = self.geometry_storage.get_all_feature_ids()
        if not all_fids:
            print("没有找到要素数据")
            return {}

        # 限制测试数量
        test_fids = all_fids[:test_count]
        feature_count = len(test_fids)

        start_time = time.time()

        # 读取几何数据
        geom_start = time.time()
        geometries = []
        for fid in test_fids:
            try:
                geom = self.geometry_storage.read_geometry(fid)
                geometries.append(geom)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取几何数据失败 FID {fid}: {e}")
        geom_time = time.time() - geom_start

        # 读取属性数据
        attr_start = time.time()
        attributes = []
        for fid in test_fids:
            try:
                attr = self.attribute_storage.read_attribute(fid)
                attributes.append(attr)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取属性数据失败 FID {fid}: {e}")
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        # 计算性能指标
        geom_rate = feature_count / geom_time if geom_time > 0 else 0
        attr_rate = feature_count / attr_time if attr_time > 0 else 0
        total_rate = feature_count / total_time if total_time > 0 else 0

        print(
            f"自定义 - 几何: {geom_rate:.1f} 要素/秒, 属性: {attr_rate:.1f} 要素/秒, 总计: {total_rate:.1f} 要素/秒"
        )

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": geom_rate,
            "attr_rate": attr_rate,
            "total_rate": total_rate,
        }

    def test_ogr_random_read(self, test_count: int = 1000) -> Dict:
        """测试OGR随机读取性能"""
        print(f"\n=== OGR随机读取测试 ({test_count}个要素) ===")

        # 获取所有要素
        self.ogr_layer.ResetReading()
        all_features = []
        for feature in self.ogr_layer:
            all_features.append(feature.Clone())

        if not all_features:
            print("没有找到要素数据")
            return {}

        # 随机选择要素进行测试
        import random

        test_features = random.sample(all_features, min(test_count, len(all_features)))
        feature_count = len(test_features)

        start_time = time.time()

        # 读取几何数据
        geom_start = time.time()
        geometries = []
        for feature in test_features:
            geom = feature.GetGeometryRef()
            if geom:
                geometries.append(geom.Clone())
        geom_time = time.time() - geom_start

        # 读取属性数据
        attr_start = time.time()
        attributes = []
        for feature in test_features:
            attrs = {}
            for j in range(feature.GetFieldCount()):
                field_def = feature.GetFieldDefnRef(j)
                field_name = field_def.GetName()
                field_value = feature.GetField(j)
                attrs[field_name] = field_value
            attributes.append(attrs)
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        # 计算性能指标
        geom_rate = feature_count / geom_time if geom_time > 0 else 0
        attr_rate = feature_count / attr_time if attr_time > 0 else 0
        total_rate = feature_count / total_time if total_time > 0 else 0

        print(
            f"OGR - 几何: {geom_rate:.1f} 要素/秒, 属性: {attr_rate:.1f} 要素/秒, 总计: {total_rate:.1f} 要素/秒"
        )

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": geom_rate,
            "attr_rate": attr_rate,
            "total_rate": total_rate,
        }

    def test_custom_random_read(self, test_count: int = 1000) -> Dict:
        """测试自定义格式随机读取性能"""
        print(f"\n=== 自定义格式随机读取测试 ({test_count}个要素) ===")

        # 获取所有要素ID
        all_fids = self.geometry_storage.get_all_feature_ids()
        if not all_fids:
            print("没有找到要素数据")
            return {}

        # 随机选择要素ID进行测试
        import random

        test_fids = random.sample(all_fids, min(test_count, len(all_fids)))
        feature_count = len(test_fids)

        start_time = time.time()

        # 读取几何数据
        geom_start = time.time()
        geometries = []
        for fid in test_fids:
            try:
                geom = self.geometry_storage.read_geometry(fid)
                geometries.append(geom)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取几何数据失败 FID {fid}: {e}")
        geom_time = time.time() - geom_start

        # 读取属性数据
        attr_start = time.time()
        attributes = []
        for fid in test_fids:
            try:
                attr = self.attribute_storage.read_attribute(fid)
                attributes.append(attr)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取属性数据失败 FID {fid}: {e}")
        attr_time = time.time() - attr_start

        total_time = time.time() - start_time

        # 计算性能指标
        geom_rate = feature_count / geom_time if geom_time > 0 else 0
        attr_rate = feature_count / attr_time if attr_time > 0 else 0
        total_rate = feature_count / total_time if total_time > 0 else 0

        print(
            f"自定义 - 几何: {geom_rate:.1f} 要素/秒, 属性: {attr_rate:.1f} 要素/秒, 总计: {total_rate:.1f} 要素/秒"
        )

        return {
            "feature_count": feature_count,
            "geom_time": geom_time,
            "attr_time": attr_time,
            "total_time": total_time,
            "geom_rate": geom_rate,
            "attr_rate": attr_rate,
            "total_rate": total_rate,
        }

    def run_comprehensive_test(self, test_sizes: Optional[List[int]] = None):
        """运行综合性能测试"""
        print("=" * 60)
        print("GIS存储格式综合性能测试")
        print("=" * 60)

        # 转换Shapefile
        self.convert_shapefile_if_needed()

        # 使用指定的测试规模或默认规模
        if test_sizes is None:
            test_sizes = self.test_sizes

        # 初始化结果字典
        results = {
            "sequential": {"ogr": {}, "custom": {}},
            "random": {"ogr": {}, "custom": {}},
            "spatial": {"ogr": {}, "custom": {}},
            "compression": {},
        }

        # 1. 顺序读取性能测试
        print("\n" + "=" * 50)
        print("顺序读取性能测试")
        print("=" * 50)

        for size in test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_sequential_read, size)
            ogr_stats = self.calculate_statistics(ogr_results, "total_rate")
            results["sequential"]["ogr"][size] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.test_custom_sequential_read, size
            )
            custom_stats = self.calculate_statistics(custom_results, "total_rate")
            results["sequential"]["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    性能提升: {improvement:+.1f}%")

        # 2. 随机读取性能测试
        print("\n" + "=" * 50)
        print("随机读取性能测试")
        print("=" * 50)

        for size in test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_random_read, size)
            ogr_stats = self.calculate_statistics(ogr_results, "total_rate")
            results["random"]["ogr"][size] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(self.test_custom_random_read, size)
            custom_stats = self.calculate_statistics(custom_results, "total_rate")
            results["random"]["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    性能提升: {improvement:+.1f}%")

        # 3. 空间查询性能测试
        print("\n" + "=" * 50)
        print("空间查询性能测试")
        print("=" * 50)

        for i, bbox in enumerate(self.test_bboxes):
            print(f"\n查询范围 {i+1}: {bbox}")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_spatial_query, bbox)
            ogr_stats = self.calculate_statistics(ogr_results, "query_rate")
            results["spatial"]["ogr"][i] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.test_custom_spatial_query, bbox
            )
            custom_stats = self.calculate_statistics(custom_results, "query_rate")
            results["spatial"]["custom"][i] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    查询速度提升: {improvement:+.1f}%")

        # 4. 字符串池压缩统计
        compression_stats = self.test_string_pool_compression()
        results["compression"] = compression_stats

        # 保存结果
        self.save_results(results)

        # 生成图表
        self.generate_charts(results)

        print("\n" + "=" * 60)
        print("测试完成")
        print("=" * 60)

    def test_ogr_spatial_query(self, bbox: Tuple[float, float, float, float]) -> Dict:
        """测试OGR空间查询性能"""
        print(f"\n=== OGR空间查询测试 (bbox: {bbox}) ===")

        # 设置空间过滤器
        self.ogr_layer.SetSpatialFilterRect(*bbox)

        start_time = time.time()
        feature_count = 0
        for _ in self.ogr_layer:
            feature_count += 1
        query_time = time.time() - start_time

        # 清除空间过滤器
        self.ogr_layer.SetSpatialFilter(None)

        # 计算性能指标
        query_rate = feature_count / query_time if query_time > 0 else 0

        print(
            f"OGR - 结果: {feature_count}个要素, 耗时: {query_time*1000:.2f}ms, 速率: {query_rate:.1f} 要素/秒"
        )

        return {
            "feature_count": feature_count,
            "query_time": query_time,
            "query_rate": query_rate,
            "bbox": bbox,
        }

    def test_custom_spatial_query(
        self, bbox: Tuple[float, float, float, float]
    ) -> Dict:
        """测试自定义格式空间查询性能"""
        print(f"\n=== 自定义格式空间查询测试 (bbox: {bbox}) ===")

        start_time = time.time()

        # 使用S2索引进行空间查询
        try:
            candidate_fids = self.s2_index.query_by_bbox(bbox)
        except AttributeError:
            # 如果S2索引没有query_by_bbox方法，使用备用方法
            print("S2索引方法不可用，使用备用查询方法")
            candidate_fids = self._backup_spatial_query(bbox)

        print(f"查询完成! 耗时: {(time.time() - start_time)*1000:.2f}ms")
        print(f"候选要素: {len(candidate_fids)}")

        # 读取候选要素的几何数据
        result_count = 0
        for fid in candidate_fids:
            try:
                geom = self.geometry_storage.read_geometry(fid)
                # 简单的边界框相交检查
                if geom.bbox:
                    geom_bbox = geom.bbox
                    if (
                        geom_bbox[0] <= bbox[2]
                        and geom_bbox[2] >= bbox[0]
                        and geom_bbox[1] <= bbox[3]
                        and geom_bbox[3] >= bbox[1]
                    ):
                        result_count += 1
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取几何数据失败 FID {fid}: {e}")

        total_time = time.time() - start_time

        # 计算性能指标
        query_rate = result_count / total_time if total_time > 0 else 0

        print(
            f"自定义 - 候选: {len(candidate_fids)}个, 结果: {result_count}个要素, 耗时: {total_time*1000:.2f}ms, 速率: {query_rate:.1f} 要素/秒"
        )

        return {
            "candidate_count": len(candidate_fids),
            "result_count": result_count,
            "query_time": total_time,
            "query_rate": query_rate,
            "bbox": bbox,
        }

    def _backup_spatial_query(
        self, bbox: Tuple[float, float, float, float]
    ) -> List[int]:
        """备用的空间查询方法，当S2索引不可用时使用"""
        candidate_fids = []
        all_fids = self.geometry_storage.get_all_feature_ids()

        for fid in all_fids:
            try:
                geom = self.geometry_storage.read_geometry(fid)
                if geom and geom.bbox:
                    geom_bbox = geom.bbox
                    if (
                        geom_bbox[0] <= bbox[2]
                        and geom_bbox[2] >= bbox[0]
                        and geom_bbox[1] <= bbox[3]
                        and geom_bbox[3] >= bbox[1]
                    ):
                        candidate_fids.append(fid)
            except (ValueError, RuntimeError, IOError) as e:
                print(f"读取几何数据失败 FID {fid}: {e}")
                continue

        return candidate_fids

    def test_string_pool_compression(self) -> Dict:
        """测试字符串池压缩效果"""
        print("\n========================================")
        print("4. 字符串池压缩统计")
        print("========================================")

        # 获取字符串池统计信息
        string_pool = self.attribute_storage.serializer.string_pool
        unique_count = string_pool.get_pool_size()
        # 安全地获取字符串表长度
        try:
            total_count = (
                len(string_pool.string_table)
                if hasattr(string_pool, "string_table")
                else unique_count
            )
        except AttributeError:
            total_count = unique_count
        total_size = string_pool.get_total_size()

        # 计算压缩率
        if total_count > 0:
            compression_ratio = (1 - unique_count / total_count) * 100
            saved_space = total_size * (1 - unique_count / total_count)
        else:
            compression_ratio = 0
            saved_space = 0

        print("字符串池统计:")
        print(f"  唯一字符串数: {unique_count}")
        print(f"  总字符串数: {total_count}")
        print(f"  压缩率: {compression_ratio:.2f}%")
        print(f"  节省空间: {saved_space:.0f} 字节")

        # 获取存储统计信息
        all_fids = self.geometry_storage.get_all_feature_ids()
        feature_count = len(all_fids)

        print("存储统计:")
        print(f"  总要素数: {feature_count}")
        print(f"  字符串池大小: {unique_count}")
        print(f"  总节省空间: {saved_space:.0f} 字节")

        return {
            "unique_strings": unique_count,
            "total_strings": total_count,
            "compression_ratio": compression_ratio,
            "saved_space": saved_space,
            "feature_count": feature_count,
        }

    def run_basic_benchmark(self):
        """运行基础性能测试"""
        print("=" * 60)
        print("基础性能测试")
        print("=" * 60)

        # 转换Shapefile
        self.convert_shapefile_if_needed()

        # 基础测试
        test_sizes = [100, 1000, 10000]

        for size in test_sizes:
            print(f"\n--- 测试规模: {size}个要素 ---")

            # 顺序读取测试
            self.test_ogr_sequential_read(size)
            self.test_custom_sequential_read(size)

            # 随机读取测试
            self.test_ogr_random_read(size)
            self.test_custom_random_read(size)

        # 空间查询测试
        for i, bbox in enumerate(self.test_bboxes[:1]):  # 只测试第一个范围
            print(f"\n--- 查询范围 {i+1}: {bbox} ---")
            self.test_ogr_spatial_query(bbox)
            self.test_custom_spatial_query(bbox)

        # 字符串池压缩统计
        self.test_string_pool_compression()

    def run_detailed_benchmark(self):
        """运行详细性能测试"""
        print("=" * 60)
        print("详细性能分析")
        print("=" * 60)

        # 转换Shapefile
        self.convert_shapefile_if_needed()

        results = {
            "sequential": {"ogr": {}, "custom": {}},
            "random": {"ogr": {}, "custom": {}},
            "spatial": {"ogr": {}, "custom": {}},
            "compression": {},
        }

        # 顺序读取性能测试
        print("\n" + "=" * 50)
        print("顺序读取性能测试")
        print("=" * 50)

        for size in self.test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_sequential_read, size)
            ogr_stats = self.calculate_statistics(ogr_results, "total_rate")
            results["sequential"]["ogr"][size] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.test_custom_sequential_read, size
            )
            custom_stats = self.calculate_statistics(custom_results, "total_rate")
            results["sequential"]["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    性能提升: {improvement:+.1f}%")

        # 随机读取性能测试
        print("\n" + "=" * 50)
        print("随机读取性能测试")
        print("=" * 50)

        for size in self.test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_random_read, size)
            ogr_stats = self.calculate_statistics(ogr_results, "total_rate")
            results["random"]["ogr"][size] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(self.test_custom_random_read, size)
            custom_stats = self.calculate_statistics(custom_results, "total_rate")
            results["random"]["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    性能提升: {improvement:+.1f}%")

        # 空间查询性能测试
        print("\n" + "=" * 50)
        print("空间查询性能测试")
        print("=" * 50)

        for i, bbox in enumerate(self.test_bboxes):
            print(f"\n查询范围 {i+1}: {bbox}")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(self.test_ogr_spatial_query, bbox)
            ogr_stats = self.calculate_statistics(ogr_results, "query_rate")
            results["spatial"]["ogr"][i] = ogr_stats

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.test_custom_spatial_query, bbox
            )
            custom_stats = self.calculate_statistics(custom_results, "query_rate")
            results["spatial"]["custom"][i] = custom_stats

            # 计算性能提升
            if ogr_stats.get("mean", 0) > 0 and custom_stats.get("mean", 0) > 0:
                improvement = (
                    (custom_stats["mean"] - ogr_stats["mean"]) / ogr_stats["mean"]
                ) * 100
                print(f"    查询速度提升: {improvement:+.1f}%")

        # 字符串池压缩统计
        compression_stats = self.test_string_pool_compression()
        results["compression"] = compression_stats

        # 保存结果
        self.save_results(results)

        # 生成图表
        self.generate_charts(results)

    def save_results(self, results: Dict):
        """保存测试结果"""
        results_file = f"{self.results_dir}/benchmark_results.json"
        with open(results_file, "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2, ensure_ascii=False)
        print(f"\n测试结果已保存到: {results_file}")

    def generate_charts(self, results: Dict):
        """生成性能对比图表"""
        print("\n" + "=" * 50)
        print("生成性能对比图表")
        print("=" * 50)

        try:
            # 使用默认字体设置，避免字体问题
            plt.rcParams["font.sans-serif"] = ["DejaVu Sans", "Arial", "Helvetica"]
            plt.rcParams["axes.unicode_minus"] = False

            fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(15, 12))

            # 使用英文标题避免字体问题
            fig.suptitle("GIS Storage Format Performance Comparison", fontsize=16)

            # 1. 顺序读取性能对比
            sizes = list(results["sequential"]["ogr"].keys())
            ogr_rates = [
                results["sequential"]["ogr"][size].get("mean", 0) for size in sizes
            ]
            custom_rates = [
                results["sequential"]["custom"][size].get("mean", 0) for size in sizes
            ]

            ax1.plot(sizes, ogr_rates, "o-", label="OGR", linewidth=2, markersize=6)
            ax1.plot(
                sizes,
                custom_rates,
                "s-",
                label="Custom Format",
                linewidth=2,
                markersize=6,
            )
            ax1.set_xlabel("Test Scale (Features)")
            ax1.set_ylabel("Read Rate (Features/sec)")
            ax1.set_title("Sequential Read Performance")
            ax1.legend()
            ax1.grid(True, alpha=0.3)

            # 2. 随机读取性能对比
            ogr_rates = [
                results["random"]["ogr"][size].get("mean", 0) for size in sizes
            ]
            custom_rates = [
                results["random"]["custom"][size].get("mean", 0) for size in sizes
            ]

            ax2.plot(sizes, ogr_rates, "o-", label="OGR", linewidth=2, markersize=6)
            ax2.plot(
                sizes,
                custom_rates,
                "s-",
                label="Custom Format",
                linewidth=2,
                markersize=6,
            )
            ax2.set_xlabel("Test Scale (Features)")
            ax2.set_ylabel("Read Rate (Features/sec)")
            ax2.set_title("Random Read Performance")
            ax2.legend()
            ax2.grid(True, alpha=0.3)

            # 3. 性能提升百分比
            sequential_improvements = []
            random_improvements = []
            for size in sizes:
                ogr_seq = results["sequential"]["ogr"][size].get("mean", 0)
                custom_seq = results["sequential"]["custom"][size].get("mean", 0)
                if ogr_seq > 0:
                    seq_imp = ((custom_seq - ogr_seq) / ogr_seq) * 100
                    sequential_improvements.append(seq_imp)
                else:
                    sequential_improvements.append(0)

                ogr_rand = results["random"]["ogr"][size].get("mean", 0)
                custom_rand = results["random"]["custom"][size].get("mean", 0)
                if ogr_rand > 0:
                    rand_imp = ((custom_rand - ogr_rand) / ogr_rand) * 100
                    random_improvements.append(rand_imp)
                else:
                    random_improvements.append(0)

            ax3.bar(
                [f"{size}" for size in sizes],
                sequential_improvements,
                label="Sequential Read",
                alpha=0.7,
                color="skyblue",
            )
            ax3.bar(
                [f"{size}" for size in sizes],
                random_improvements,
                label="Random Read",
                alpha=0.7,
                color="lightcoral",
            )
            ax3.set_xlabel("Test Scale (Features)")
            ax3.set_ylabel("Performance Improvement (%)")
            ax3.set_title("Performance Improvement Comparison")
            ax3.legend()
            ax3.grid(True, alpha=0.3)

            # 4. 空间查询性能对比
            bbox_labels = ["Small Range", "Medium Range", "Large Range"]
            ogr_rates = [
                results["spatial"]["ogr"][i].get("mean", 0)
                for i in range(len(self.test_bboxes))
            ]
            custom_rates = [
                results["spatial"]["custom"][i].get("mean", 0)
                for i in range(len(self.test_bboxes))
            ]

            x = np.arange(len(bbox_labels))
            width = 0.35

            ax4.bar(x - width / 2, ogr_rates, width, label="OGR", alpha=0.7)
            ax4.bar(
                x + width / 2, custom_rates, width, label="Custom Format", alpha=0.7
            )
            ax4.set_xlabel("Query Range")
            ax4.set_ylabel("Query Rate (Features/sec)")
            ax4.set_title("Spatial Query Performance")
            ax4.set_xticks(x)
            ax4.set_xticklabels(bbox_labels)
            ax4.legend()
            ax4.grid(True, alpha=0.3)

            plt.tight_layout()

            # 保存图表
            chart_file = f"{self.results_dir}/performance_charts.png"
            plt.savefig(chart_file, dpi=300, bbox_inches="tight")
            print(f"性能图表已保存到: {chart_file}")

            plt.show()

        except (ValueError, RuntimeError, IOError, ImportError) as e:
            print(f"生成图表失败: {e}")
            import traceback

            traceback.print_exc()


def run_basic_test(shapefile_path: str, output_dir: str = "./output_data"):
    """运行基础性能测试"""
    print("运行基础性能测试...")
    benchmark = GISBenchmark(shapefile_path, output_dir)
    benchmark.run_comprehensive_test()


def run_detailed_test(shapefile_path: str, output_dir: str = "./output_data"):
    """运行详细性能测试"""
    print("运行详细性能测试...")
    benchmark = GISBenchmark(shapefile_path, output_dir)
    benchmark.test_iterations = 10  # 增加测试次数
    benchmark.run_comprehensive_test()


def run_quick_test(shapefile_path: str, output_dir: str = "./output_data"):
    """运行快速性能测试"""
    print("运行快速性能测试...")
    benchmark = GISBenchmark(shapefile_path, output_dir)
    benchmark.test_iterations = 3  # 减少测试次数
    benchmark.run_comprehensive_test(test_sizes=[100, 1000])


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="GIS存储格式性能测试工具")
    parser.add_argument(
        "--shapefile",
        "-s",
        default="./data/test.shp",
        help="Shapefile文件路径 (默认: ./data/test.shp)",
    )
    parser.add_argument(
        "--test-type",
        "-t",
        choices=["basic", "detailed", "quick"],
        default="basic",
        help="测试类型 (默认: basic)",
    )
    parser.add_argument(
        "--output-dir",
        "-o",
        default="./output_data",
        help="输出目录 (默认: ./output_data)",
    )

    args = parser.parse_args()

    # 检查文件是否存在
    if not os.path.exists(args.shapefile):
        print(f"错误: 找不到Shapefile文件 {args.shapefile}")
        print("请确保数据文件存在，或使用 --shapefile 参数指定正确的文件路径")
        return

    print("=" * 60)
    print("GIS存储格式性能测试")
    print("=" * 60)
    print(f"数据文件: {args.shapefile}")
    print(f"测试类型: {args.test_type}")
    print(f"输出目录: {args.output_dir}")
    print("=" * 60)

    try:
        if args.test_type == "basic":
            run_basic_test(args.shapefile, args.output_dir)
        elif args.test_type == "detailed":
            run_detailed_test(args.shapefile, args.output_dir)
        elif args.test_type == "quick":
            run_quick_test(args.shapefile, args.output_dir)

        print("\n测试完成！")

    except (ValueError, RuntimeError, IOError, ImportError) as e:
        print(f"测试过程中发生错误: {e}")
        import traceback

        traceback.print_exc()


if __name__ == "__main__":
    main()
