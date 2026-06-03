#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GIS存储系统运行器 - 提供命令行接口和示例用法
与C++版本兼容的Python实现
"""

import os
import sys
import argparse

# 导入新的模块结构
import sys
import os

# 添加py目录到Python路径
current_dir = os.path.dirname(os.path.abspath(__file__))
py_dir = os.path.dirname(current_dir)
if py_dir not in sys.path:
    sys.path.insert(0, py_dir)

from gisstorage.gis_storage_system import GisStorageSystem
from gisstorage.ogr_format_converter import OGRFormatConverter


class GisStorageRunner:
    """GIS存储系统运行器类"""

    def __init__(self):
        self.storage_system: GisStorageSystem = None
        self.converter: OGRFormatConverter = None

    def convert_shapefile(self, input_file: str, output_dir: str) -> bool:
        """转换Shapefile到自定义格式"""
        print(f"开始转换Shapefile: {input_file}")
        print(f"输出目录: {output_dir}")

        try:
            # 创建转换器
            self.converter = OGRFormatConverter(input_file, output_dir)

            # 执行转换
            success = self.converter.convert_shapefile_to_custom_format()

            if success:
                # 显示转换统计信息
                stats = self.converter.get_conversion_stats()
                print("\n转换完成!")
                print(f"总要素数: {stats['total_features']}")
                print(f"有效要素数: {stats['valid_features']}")
                print(f"转换时间: {stats['conversion_time_seconds']:.2f} 秒")

                # 显示文件信息
                self._show_output_files(output_dir)

                return True
            else:
                print("转换失败!")
                return False

        except (IOError, RuntimeError, ValueError) as e:
            print(f"转换过程中出错: {e}")
            return False

    def load_custom_format(self, data_dir: str, dataset_name: str = "data") -> bool:
        """加载自定义格式数据"""
        print(f"加载自定义格式数据: {data_dir}")

        try:
            # 创建存储系统
            self.storage_system = GisStorageSystem(data_dir, dataset_name)

            # 加载元数据
            metadata = self.storage_system.load_metadata()
            if not metadata:
                print("无法加载元数据")
                return False

            print(f"成功加载数据集: {dataset_name}")
            print(f"总要素数: {metadata.get('total_features', 0)}")
            print(f"有效要素数: {metadata.get('valid_features', 0)}")

            return True

        except (IOError, RuntimeError, ValueError) as e:
            print(f"加载数据时出错: {e}")
            return False

    def query_geometry(self, feature_id: int) -> bool:
        """查询几何数据"""
        if not self.storage_system:
            print("请先加载数据")
            return False

        try:
            print(f"查询几何数据: FID {feature_id}")

            # 读取几何数据
            geometry = self.storage_system.read_geometry(feature_id)

            print(f"几何类型: {geometry.get_geometry_type().name}")
            print(f"边界框: {geometry.get_bbox()}")
            print(f"环数量: {geometry.get_num_rings()}")

            # 解码坐标
            coordinates = geometry.decode_coordinates()
            print(f"坐标点数量: {len(coordinates)}")

            if len(coordinates) <= 10:
                print("坐标点:")
                for i, coord in enumerate(coordinates):
                    print(f"  {i}: ({coord.x:.6f}, {coord.y:.6f})")
            else:
                print("前5个坐标点:")
                for i, coord in enumerate(coordinates[:5]):
                    print(f"  {i}: ({coord.x:.6f}, {coord.y:.6f})")
                print(f"  ... 还有 {len(coordinates) - 5} 个坐标点")

            return True

        except (ValueError, RuntimeError) as e:
            print(f"查询几何数据时出错: {e}")
            return False

    def query_attribute(self, feature_id: int) -> bool:
        """查询属性数据"""
        if not self.storage_system:
            print("请先加载数据")
            return False

        try:
            print(f"查询属性数据: FID {feature_id}")

            # 读取属性数据
            attribute = self.storage_system.read_attribute(feature_id)

            properties = attribute.get_properties()
            print(f"属性字段数量: {len(properties)}")

            print("属性数据:")
            for key, value in properties.items():
                print(f"  {key}: {value}")

            return True

        except (ValueError, RuntimeError) as e:
            print(f"查询属性数据时出错: {e}")
            return False

    def query_by_attribute(self, field_name: str, field_value: str) -> bool:
        """按属性查询"""
        if not self.storage_system:
            print("请先加载数据")
            return False

        try:
            print(f"按属性查询: {field_name} = '{field_value}'")

            # 执行查询
            results = self.storage_system.query_by_attribute_efficient(
                field_name, field_value
            )

            print(f"找到 {len(results)} 个匹配要素")

            if len(results) <= 10:
                print("匹配的要素ID:")
                for fid in results:
                    print(f"  FID: {fid}")
            else:
                print("前10个匹配的要素ID:")
                for fid in results[:10]:
                    print(f"  FID: {fid}")
                print(f"  ... 还有 {len(results) - 10} 个要素")

            return True

        except (ValueError, RuntimeError) as e:
            print(f"按属性查询时出错: {e}")
            return False

    def show_storage_stats(self) -> bool:
        """显示存储统计信息"""
        if not self.storage_system:
            print("请先加载数据")
            return False

        try:
            print("存储统计信息:")

            # 获取存储统计
            stats = self.storage_system.get_storage_stats()

            print("几何存储:")
            geom_stats = stats.get("geometry_stats", {})
            print(f"  文件路径: {geom_stats.get('file_path', 'N/A')}")
            print(f"  缓存大小: {geom_stats.get('cache_size', 0)}")
            print(f"  最大缓存: {geom_stats.get('max_cache_size', 0)}")

            print("属性存储:")
            attr_stats = stats.get("attribute_stats", {})
            print(f"  总要素数: {attr_stats.get('total_features', 0)}")
            print(f"  压缩率: {attr_stats.get('compression_ratio', 0.0):.2f}%")
            print(f"  字符串池大小: {attr_stats.get('string_pool_size', 0)}")
            print(f"  节省字节数: {attr_stats.get('string_pool_saved_bytes', 0)}")

            return True

        except (ValueError, RuntimeError) as e:
            print(f"获取存储统计时出错: {e}")
            return False

    def show_metadata(self) -> bool:
        """显示元数据信息"""
        if not self.storage_system:
            print("请先加载数据")
            return False

        try:
            print("元数据信息:")

            # 获取元数据
            metadata = self.storage_system.get_metadata()

            print(f"格式版本: {metadata.get('format_version', 'N/A')}")
            print(f"源格式: {metadata.get('source_format', 'N/A')}")
            print(f"源文件: {metadata.get('source_file', 'N/A')}")
            print(f"创建时间: {metadata.get('creation_date', 'N/A')}")
            print(f"总要素数: {metadata.get('total_features', 0)}")
            print(f"有效要素数: {metadata.get('valid_features', 0)}")

            # 空间范围
            spatial_extent = metadata.get("spatial_extent", {})
            if spatial_extent:
                print("空间范围:")
                print(f"  最小X: {spatial_extent.get('min_x', 0.0)}")
                print(f"  最小Y: {spatial_extent.get('min_y', 0.0)}")
                print(f"  最大X: {spatial_extent.get('max_x', 0.0)}")
                print(f"  最大Y: {spatial_extent.get('max_y', 0.0)}")

            return True

        except (ValueError, RuntimeError) as e:
            print(f"获取元数据时出错: {e}")
            return False

    def _show_output_files(self, output_dir: str) -> None:
        """显示输出文件信息"""
        print("\n输出文件:")

        # 查找所有相关文件
        files = [
            ".geom",
            ".attr",
            ".pool",
            ".pool.index",
            "_meta.json",
            ".geom.chunked_idx",
            ".attr.chunked_idx",
        ]

        for ext in files:
            # 查找匹配的文件
            for filename in os.listdir(output_dir):
                if filename.endswith(ext):
                    filepath = os.path.join(output_dir, filename)
                    size = os.path.getsize(filepath)
                    print(f"  {filename}: {size:,} 字节")


def main():
    """主函数 - 命令行接口"""
    parser = argparse.ArgumentParser(description="GIS存储系统运行器")
    subparsers = parser.add_subparsers(dest="command", help="可用命令")

    # 转换命令
    convert_parser = subparsers.add_parser("convert", help="转换Shapefile到自定义格式")
    convert_parser.add_argument("input_file", help="输入Shapefile路径")
    convert_parser.add_argument("output_dir", help="输出目录")

    # 加载命令
    load_parser = subparsers.add_parser("load", help="加载自定义格式数据")
    load_parser.add_argument("data_dir", help="数据目录")
    load_parser.add_argument("--dataset", default="data", help="数据集名称")

    # 查询几何命令
    geom_parser = subparsers.add_parser("geom", help="查询几何数据")
    geom_parser.add_argument("feature_id", type=int, help="要素ID")

    # 查询属性命令
    attr_parser = subparsers.add_parser("attr", help="查询属性数据")
    attr_parser.add_argument("feature_id", type=int, help="要素ID")

    # 按属性查询命令
    query_parser = subparsers.add_parser("query", help="按属性查询")
    query_parser.add_argument("field_name", help="字段名")
    query_parser.add_argument("field_value", help="字段值")

    # 统计命令
    subparsers.add_parser("stats", help="显示存储统计信息")

    # 元数据命令
    subparsers.add_parser("meta", help="显示元数据信息")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    # 创建运行器
    runner = GisStorageRunner()

    # 执行命令
    if args.command == "convert":
        success = runner.convert_shapefile(args.input_file, args.output_dir)
        sys.exit(0 if success else 1)

    elif args.command == "load":
        success = runner.load_custom_format(args.data_dir, args.dataset)
        if not success:
            sys.exit(1)

    elif args.command == "geom":
        success = runner.query_geometry(args.feature_id)
        sys.exit(0 if success else 1)

    elif args.command == "attr":
        success = runner.query_attribute(args.feature_id)
        sys.exit(0 if success else 1)

    elif args.command == "query":
        success = runner.query_by_attribute(args.field_name, args.field_value)
        sys.exit(0 if success else 1)

    elif args.command == "stats":
        success = runner.show_storage_stats()
        sys.exit(0 if success else 1)

    elif args.command == "meta":
        success = runner.show_metadata()
        sys.exit(0 if success else 1)


def demo():
    """演示函数 - 展示基本用法"""
    print("=== GIS存储系统演示 ===")

    # 创建运行器
    runner = GisStorageRunner()

    # 示例数据目录
    data_dir = "output_data/convert_test"

    # 检查是否存在测试数据
    if not os.path.exists(data_dir):
        print(f"测试数据目录不存在: {data_dir}")
        print("请先运行转换命令创建测试数据")
        return

    # 加载数据
    print("\n1. 加载自定义格式数据...")
    if not runner.load_custom_format(data_dir, "test"):
        print("加载数据失败")
        return

    # 显示元数据
    print("\n2. 显示元数据信息...")
    runner.show_metadata()

    # 显示存储统计
    print("\n3. 显示存储统计信息...")
    runner.show_storage_stats()

    # 查询几何数据
    print("\n4. 查询几何数据 (FID=1)...")
    runner.query_geometry(1)

    # 查询属性数据
    print("\n5. 查询属性数据 (FID=1)...")
    runner.query_attribute(1)

    print("\n=== 演示完成 ===")


if __name__ == "__main__":
    if len(sys.argv) == 1:
        # 没有参数时运行演示
        demo()
    else:
        # 有参数时运行命令行接口
        main()
