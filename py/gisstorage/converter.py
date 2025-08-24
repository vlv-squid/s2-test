# converter.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import os
import json
import struct
from typing import Tuple, List, Dict
from osgeo import ogr

from gisstorage.models import GeometryData, AttributeData, GeometryType
from gisstorage.serializers import GeometrySerializer


class ShapefileConverter:
    """Shapefile转换器，与C++版本兼容"""

    def __init__(self, shapefile_path: str, output_dir: str):
        from gisstorage.storage import GeometryStorage, AttributeStorage

        # 提取shapefile文件名（不含扩展名）作为前缀
        shapefile_name = os.path.splitext(os.path.basename(shapefile_path))[0]

        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)

        # 构造文件路径
        geom_file_path = os.path.join(output_dir, f"{shapefile_name}_geom.dat")
        attr_file_path = os.path.join(output_dir, f"{shapefile_name}_attr.dat")
        pool_file_path = os.path.join(output_dir, f"{shapefile_name}_pool.dat")
        self.index_file = os.path.join(output_dir, f"{shapefile_name}_index.dat")

        # 保存shapefile路径和名称
        self.shapefile_path = shapefile_path
        self.shapefile_name = shapefile_name

        # 初始化存储管理器
        self.geometry_storage = GeometryStorage(geom_file_path)
        self.attribute_storage = AttributeStorage(attr_file_path, pool_file_path)

        # 统计信息
        self.stats = {
            "total_features": 0,
            "valid_features": 0,
            "geometry_size": 0,
            "attribute_original_size": 0,
            "attribute_compressed_size": 0,
            "compression_ratio": 0.0,
            "string_pool_size": 0,
            "string_pool_saved_bytes": 0,
            "conversion_time_seconds": 0.0,
        }

    def _get_geometry_type(self, ogr_geometry_type: int) -> GeometryType:
        """将OGR几何类型转换为内部几何类型枚举"""
        if ogr_geometry_type in [ogr.wkbPoint, ogr.wkbPoint25D]:
            return GeometryType.POINT
        elif ogr_geometry_type in [ogr.wkbLineString, ogr.wkbLineString25D]:
            return GeometryType.LINE
        elif ogr_geometry_type in [ogr.wkbPolygon, ogr.wkbPolygon25D]:
            return GeometryType.POLYGON
        elif ogr_geometry_type in [ogr.wkbMultiPoint, ogr.wkbMultiPoint25D]:
            return GeometryType.MULTIPOINT
        elif ogr_geometry_type in [ogr.wkbMultiLineString, ogr.wkbMultiLineString25D]:
            return GeometryType.MULTILINE
        elif ogr_geometry_type in [ogr.wkbMultiPolygon, ogr.wkbMultiPolygon25D]:
            return GeometryType.MULTIPOLYGON
        else:
            return GeometryType.POINT  # 默认类型

    def _extract_coordinates(self, geometry) -> List[Tuple[float, float]]:
        """提取几何坐标，与C++版本兼容"""
        if not geometry:
            return []

        coordinates = []
        geom_type = geometry.GetGeometryType()

        if geom_type in [ogr.wkbPoint, ogr.wkbPoint25D]:
            point = geometry.GetPoint(0)
            coordinates.append((point[0], point[1]))
        elif geom_type in [ogr.wkbLineString, ogr.wkbLineString25D]:
            line = geometry
            try:
                # 使用更安全的方法获取坐标
                for i in range(line.GetPointCount()):
                    point = line.GetPoint(i)
                    if len(point) >= 2:
                        coordinates.append((point[0], point[1]))
                    else:
                        print(f"警告: 点 {i} 坐标不足")
            except Exception as e:
                print(f"警告: 获取线坐标失败: {e}")
                # 尝试备用方法
                try:
                    point = line.GetPoint(0)
                    if len(point) >= 2:
                        coordinates.append((point[0], point[1]))
                except:
                    pass
        elif geom_type in [ogr.wkbPolygon, ogr.wkbPolygon25D]:
            polygon = geometry
            ring = polygon.GetGeometryRef(0)  # 外环
            if ring:
                for i in range(ring.GetPointCount()):
                    point = ring.GetPoint(i)
                    coordinates.append((point[0], point[1]))
        elif geom_type in [
            ogr.wkbMultiPoint,
            ogr.wkbMultiPoint25D,
            ogr.wkbMultiLineString,
            ogr.wkbMultiLineString25D,
            ogr.wkbMultiPolygon,
            ogr.wkbMultiPolygon25D,
        ]:
            # 处理复合几何
            for i in range(geometry.GetGeometryCount()):
                sub_geom = geometry.GetGeometryRef(i)
                sub_coords = self._extract_coordinates(sub_geom)
                coordinates.extend(sub_coords)

        return coordinates

    def convert(self) -> List[int]:
        """转换Shapefile，与C++版本兼容"""
        import time

        start_time = time.time()

        print(f"开始转换Shapefile（优化版本）: {self.shapefile_path}")

        # 打开Shapefile
        dataset = ogr.Open(self.shapefile_path)
        if not dataset:
            raise ValueError(f"无法打开Shapefile: {self.shapefile_path}")

        layer = dataset.GetLayer(0)
        if not layer:
            dataset.Close()
            raise ValueError("无法获取图层")

        # 获取字段信息
        feature_defn = layer.GetLayerDefn()
        field_count = feature_defn.GetFieldCount()

        # 构建字段信息
        field_info = {}
        for i in range(field_count):
            field_defn = feature_defn.GetFieldDefn(i)
            field_info[field_defn.GetName()] = field_defn.GetType()

        # 初始化索引数据
        index_data = {"version": 1, "data": {"features": {}}}

        valid_fids = []
        total_features = layer.GetFeatureCount()
        self.stats["total_features"] = total_features
        print(f"共 {total_features} 个要素")

        # 重置图层
        layer.ResetReading()

        processed_count = 0
        feature = layer.GetNextFeature()

        while feature:
            fid = feature.GetFID()

            try:
                # 提取几何数据
                geometry = feature.GetGeometryRef()
                if not geometry or geometry.IsEmpty():
                    feature = layer.GetNextFeature()
                    continue

                coordinates = self._extract_coordinates(geometry)
                if not coordinates:
                    feature = layer.GetNextFeature()
                    continue

                # 计算边界框
                bbox = GeometrySerializer.calculate_bbox(coordinates)

                # 压缩坐标数据
                coord_data = GeometrySerializer.encode_coordinates_delta(coordinates)

                # 确定几何类型
                geom_type = self._get_geometry_type(geometry.GetGeometryType())

                # 创建几何数据对象
                geom_data = GeometryData(fid, geom_type, coord_data, bbox)

                # 写入几何数据
                geom_offset = self.geometry_storage.write_geometry(geom_data)

                # 提取属性数据
                properties = {}
                for i in range(field_count):
                    field_defn = feature_defn.GetFieldDefn(i)
                    field_name = field_defn.GetName()

                    if feature.IsFieldSetAndNotNull(i):
                        field_value = ""
                        field_type = field_defn.GetType()

                        if field_type == ogr.OFTInteger:
                            field_value = str(feature.GetFieldAsInteger(i))
                        elif field_type == ogr.OFTInteger64:
                            field_value = str(feature.GetFieldAsInteger64(i))
                        elif field_type == ogr.OFTReal:
                            field_value = str(feature.GetFieldAsDouble(i))
                        elif field_type == ogr.OFTString:
                            field_value = feature.GetFieldAsString(i)
                        elif field_type in [ogr.OFTDate, ogr.OFTTime, ogr.OFTDateTime]:
                            field_value = feature.GetFieldAsString(i)
                        else:
                            field_value = feature.GetFieldAsString(i)

                        properties[field_name] = field_value

                # 创建属性数据对象
                attr_data = AttributeData(fid, properties)

                # 写入属性数据（使用字符串池优化）
                attr_offset = self.attribute_storage.write_attribute(attr_data)

                # 添加到索引
                index_data["data"]["features"][str(fid)] = {
                    "geom_offset": geom_offset,
                    "attr_offset": attr_offset,
                }

                valid_fids.append(fid)

            except Exception as e:
                print(f"处理要素 {fid} 时出错: {str(e)}")

            feature = layer.GetNextFeature()
            processed_count += 1

        # 保存索引数据
        self._save_index_data(index_data)

        # 保存字符串池
        self.attribute_storage.save_string_pool()

        # 更新统计信息
        self.stats["valid_features"] = len(valid_fids)
        compression_stats = self.attribute_storage.get_compression_stats()
        self.stats["string_pool_size"] = compression_stats["unique_strings"]
        self.stats["string_pool_saved_bytes"] = (
            compression_stats["original_size"] - compression_stats["compressed_size"]
        )
        self.stats["compression_ratio"] = compression_stats["compression_ratio"]

        # 计算转换时间
        end_time = time.time()
        self.stats["conversion_time_seconds"] = end_time - start_time

        # 输出统计信息
        print("转换完成！")
        print(
            f"  有效要素: {self.stats['valid_features']}/{self.stats['total_features']}"
        )
        print(f"  字符串池大小: {self.stats['string_pool_size']} 个唯一字符串")
        print(f"  压缩率: {self.stats['compression_ratio']:.2f}%")
        print(f"  节省空间: {self.stats['string_pool_saved_bytes']} 字节")
        print(f"  转换时间: {self.stats['conversion_time_seconds']:.2f} 秒")

        # 清理
        try:
            dataset.Close()
        except AttributeError:
            # 新版本GDAL中DataSource没有Close方法
            pass

        return valid_fids

    def _save_index_data(self, index_data: Dict) -> None:
        """保存索引数据"""
        with open(self.index_file, "w", encoding="utf-8") as f:
            json.dump(index_data, f, indent=4, ensure_ascii=False)
        print(f"索引文件已保存: {self.index_file}")

    def get_geometry_file_path(self) -> str:
        """获取几何文件路径"""
        return self.geometry_storage.get_geometry_file_path()

    def get_attribute_file_path(self) -> str:
        """获取属性文件路径"""
        return self.attribute_storage.get_attribute_file_path()

    def get_string_pool_file_path(self) -> str:
        """获取字符串池文件路径"""
        return self.attribute_storage.get_string_pool_file_path()

    def get_index_file_path(self) -> str:
        """获取索引文件路径"""
        return self.index_file

    def get_compression_stats(self) -> Dict:
        """获取压缩统计信息"""
        return self.attribute_storage.get_compression_stats()

    def get_conversion_stats(self) -> Dict:
        """获取转换统计信息"""
        return self.stats.copy()
