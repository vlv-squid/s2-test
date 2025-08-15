# gisstorage_demo.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import os
import sys
import tempfile
import shutil
import pickle
import struct

# 添加项目路径
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'py'))
from gisstorage.models import GeometryData, AttributeData
from gisstorage.serializers import GeometrySerializer, AttributeSerializer
from gisstorage.storage import GeometryStorage, AttributeStorage


def demo_coordinate_serialization():
    """演示坐标数据的序列化和反序列化"""
    print("=" * 60)
    print("演示：坐标数据的序列化和反序列化")
    print("=" * 60)

    # 创建测试坐标数据
    test_coords = [(120.123456, 30.123456), (120.123457, 30.123457),
                   (120.123458, 30.123458), (120.123459, 30.123459)]

    print(f"原始坐标数据: {test_coords}")

    # 序列化
    serialized = GeometrySerializer.serialize_coordinates(test_coords)
    print(f"序列化后大小: {len(serialized)} 字节")

    # 反序列化
    deserialized, offset = GeometrySerializer.deserialize_coordinates(
        serialized)
    print(f"反序列化后坐标: {deserialized}")

    # 验证数据完整性
    is_correct = len(deserialized) == len(test_coords)
    for i, (orig, deser) in enumerate(zip(test_coords, deserialized)):
        if abs(orig[0] - deser[0]) > 1e-6 or abs(orig[1] - deser[1]) > 1e-6:
            is_correct = False
            break

    print(f"数据完整性验证: {'通过' if is_correct else '失败'}")
    print()


def demo_geometry_serialization():
    """演示几何数据的序列化和反序列化"""
    print("=" * 60)
    print("演示：几何数据的序列化和反序列化")
    print("=" * 60)

    # 创建测试几何数据
    feature_id = 12345
    geometry_type = 1  # LineString
    test_coords = [(120.0, 30.0), (120.1, 30.1), (120.2, 30.2)]
    bbox = (120.0, 30.0, 120.2, 30.2)

    print(f"要素ID: {feature_id}")
    print(f"几何类型: {geometry_type} (LineString)")
    print(f"坐标: {test_coords}")
    print(f"边界框: {bbox}")

    # 序列化坐标
    coord_bytes = GeometrySerializer.serialize_coordinates(test_coords)

    # 创建几何数据对象
    geom_data = GeometryData(feature_id, geometry_type, coord_bytes, bbox)

    # 序列化整个几何对象
    serialized = GeometrySerializer.serialize_geometry(geom_data)
    print(f"序列化后大小: {len(serialized)} 字节")

    # 反序列化
    deserialized, offset = GeometrySerializer.deserialize_geometry(serialized)

    print(f"反序列化结果:")
    print(f"  要素ID: {deserialized.feature_id}")
    print(f"  几何类型: {deserialized.geometry_type}")
    print(f"  边界框: {deserialized.bbox}")

    # 解析坐标数据
    coords, _ = GeometrySerializer.deserialize_coordinates(
        deserialized.coordinates)
    print(f"  坐标: {coords}")

    # 验证数据完整性
    is_correct = (deserialized.feature_id == feature_id
                  and deserialized.geometry_type == geometry_type
                  and deserialized.bbox == bbox
                  and len(coords) == len(test_coords))

    print(f"数据完整性验证: {'通过' if is_correct else '失败'}")
    print()


def demo_attribute_serialization():
    """演示属性数据的序列化和反序列化"""
    print("=" * 60)
    print("演示：属性数据的序列化和反序列化")
    print("=" * 60)

    # 创建测试属性数据
    feature_id = 12345
    properties = {
        "name": "测试要素",
        "type": "道路",
        "length": 1234.56,
        "category": "主干道",
        "is_active": True,
        "tags": ["重要", "城市道路"]
    }

    print(f"要素ID: {feature_id}")
    print(f"属性数据: {properties}")

    attr_data = AttributeData(feature_id, properties)

    # 序列化
    serialized = AttributeSerializer.serialize_attributes(attr_data)
    print(f"序列化后大小: {len(serialized)} 字节")

    # 反序列化
    deserialized, offset = AttributeSerializer.deserialize_attributes(
        serialized)

    print(f"反序列化结果:")
    print(f"  要素ID: {deserialized.feature_id}")
    print(f"  属性数据: {deserialized.properties}")

    # 验证数据完整性
    is_correct = (deserialized.feature_id == feature_id
                  and deserialized.properties == properties)

    print(f"数据完整性验证: {'通过' if is_correct else '失败'}")
    print()


def demo_storage_operations():
    """演示存储操作"""
    print("=" * 60)
    print("演示：存储操作")
    print("=" * 60)

    # 创建临时目录
    test_dir = tempfile.mkdtemp()
    geometry_file = os.path.join(test_dir, "test_geom.dat")
    attribute_file = os.path.join(test_dir, "test_attr.dat")

    try:
        # 初始化存储对象
        geometry_storage = GeometryStorage(geometry_file)
        attribute_storage = AttributeStorage(attribute_file)

        # 添加字段信息到属性文件（修复读取问题）
        field_info = {
            'names': ['name', 'area', 'perimeter', 'type', 'height'],
            'types': [None, None, None, None, None]
        }
        field_info_bytes = pickle.dumps(field_info)
        with open(attribute_file, 'wb') as f:
            f.write(struct.pack('I', len(field_info_bytes)))
            f.write(field_info_bytes)

        # 创建测试数据
        feature_id = 12345
        geometry_type = 2  # Polygon
        test_coords = [(120.0, 30.0), (120.1, 30.0), (120.1, 30.1),
                       (120.0, 30.1), (120.0, 30.0)]
        bbox = (120.0, 30.0, 120.1, 30.1)
        properties = {
            "name": "测试多边形",
            "area": 1000.0,
            "perimeter": 400.0,
            "type": "建筑",
            "height": 50
        }

        print(f"写入数据:")
        print(f"  要素ID: {feature_id}")
        print(f"  几何类型: {geometry_type} (Polygon)")
        print(f"  坐标: {test_coords}")
        print(f"  属性: {properties}")

        # 写入几何数据
        coord_bytes = GeometrySerializer.serialize_coordinates(test_coords)
        geom_data = GeometryData(feature_id, geometry_type, coord_bytes, bbox)
        offset = geometry_storage.write_geometry(geom_data)
        print(f"  几何数据写入偏移: {offset}")

        # 写入属性数据
        attr_data = AttributeData(feature_id, properties)
        attribute_storage.write_attribute(attr_data)
        print(f"  属性数据写入完成")

        # 读取几何数据
        read_geom = geometry_storage.read_geometry(feature_id)
        print(f"\n读取几何数据:")
        print(f"  要素ID: {read_geom.feature_id}")
        print(f"  几何类型: {read_geom.geometry_type}")
        print(f"  边界框: {read_geom.bbox}")

        # 解析坐标
        coords, _ = GeometrySerializer.deserialize_coordinates(
            read_geom.coordinates)
        print(f"  坐标: {coords}")

        # 读取属性数据
        read_props = attribute_storage.read_attribute(feature_id)
        print(f"\n读取属性数据:")
        print(f"  属性: {read_props}")

        # 验证数据完整性
        geom_correct = (read_geom.feature_id == feature_id
                        and read_geom.geometry_type == geometry_type
                        and read_geom.bbox == bbox
                        and len(coords) == len(test_coords))

        attr_correct = read_props is not None and read_props.properties == properties

        print(f"\n数据完整性验证:")
        print(f"  几何数据: {'通过' if geom_correct else '失败'}")
        print(f"  属性数据: {'通过' if attr_correct else '失败'}")

    finally:
        # 清理临时目录
        shutil.rmtree(test_dir, ignore_errors=True)

    print()


def demo_multiple_features():
    """演示多个要素的存储和读取"""
    print("=" * 60)
    print("演示：多个要素的存储和读取")
    print("=" * 60)

    # 创建临时目录
    test_dir = tempfile.mkdtemp()
    geometry_file = os.path.join(test_dir, "test_geom.dat")
    attribute_file = os.path.join(test_dir, "test_attr.dat")

    try:
        # 初始化存储对象
        geometry_storage = GeometryStorage(geometry_file)
        attribute_storage = AttributeStorage(attribute_file)

        # 添加字段信息到属性文件（修复读取问题）
        field_info = {
            'names': ['name', 'type', 'length', 'area'],
            'types': [None, None, None, None]
        }
        field_info_bytes = pickle.dumps(field_info)
        with open(attribute_file, 'wb') as f:
            f.write(struct.pack('I', len(field_info_bytes)))
            f.write(field_info_bytes)

        # 创建多个测试要素
        test_features = [(1, 0, [(120.0, 30.0)], (120.0, 30.0, 120.0, 30.0), {
            "name": "点1",
            "type": "POI"
        }),
                         (2, 1, [(120.0, 30.0),
                                 (120.1, 30.1)], (120.0, 30.0, 120.1, 30.1), {
                                     "name": "线1",
                                     "type": "道路",
                                     "length": 100.0
                                 }),
                         (3, 2, [(120.0, 30.0), (120.1, 30.0), (120.1, 30.1),
                                 (120.0, 30.1),
                                 (120.0, 30.0)], (120.0, 30.0, 120.1, 30.1), {
                                     "name": "面1",
                                     "type": "建筑",
                                     "area": 500.0
                                 })]

        print("写入多个要素:")
        for fid, geom_type, coords, bbox, props in test_features:
            print(f"  要素 {fid}: 类型={geom_type}, 坐标数={len(coords)}, 属性={props}")

            # 写入几何数据
            coord_bytes = GeometrySerializer.serialize_coordinates(coords)
            geom_data = GeometryData(fid, geom_type, coord_bytes, bbox)
            geometry_storage.write_geometry(geom_data)

            # 写入属性数据
            attr_data = AttributeData(fid, props)
            attribute_storage.write_attribute(attr_data)

        print("\n读取并验证所有要素:")
        all_correct = True

        for fid, geom_type, coords, bbox, props in test_features:
            # 读取几何数据
            read_geom = geometry_storage.read_geometry(fid)
            read_coords, _ = GeometrySerializer.deserialize_coordinates(
                read_geom.coordinates)

            # 读取属性数据
            read_props = attribute_storage.read_attribute(fid)

            # 验证
            geom_correct = (read_geom.feature_id == fid
                            and read_geom.geometry_type == geom_type
                            and read_geom.bbox == bbox
                            and len(read_coords) == len(coords))

            # 修改属性验证逻辑，确保正确比较属性数据
            attr_correct = read_props is not None and read_props.properties == props

            feature_correct = geom_correct and attr_correct
            all_correct = all_correct and feature_correct

            print(f"  要素 {fid}: {'通过' if feature_correct else '失败'}")

        print(f"\n总体验证: {'通过' if all_correct else '失败'}")

    finally:
        # 清理临时目录
        shutil.rmtree(test_dir, ignore_errors=True)

    print()


def main():
    """主函数"""
    print("GIS存储模块二进制解析功能演示")
    print("=" * 80)
    print()

    # 运行各个演示
    demo_coordinate_serialization()
    demo_geometry_serialization()
    demo_attribute_serialization()
    demo_storage_operations()
    demo_multiple_features()

    print("=" * 80)
    print("演示完成！")
    print("这些演示展示了gisstorage模块的核心功能：")
    print("1. 坐标数据的序列化和反序列化")
    print("2. 几何数据的序列化和反序列化")
    print("3. 属性数据的序列化和反序列化")
    print("4. 数据存储和读取操作")
    print("5. 多个要素的批量处理")


if __name__ == '__main__':
    main()
