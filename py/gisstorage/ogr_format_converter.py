# ogr_format_converter.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import os
import json
import time
from typing import Dict, List, Optional, Any, Tuple
from osgeo import ogr
from .geometry_types import GeometryType, Coordinate, BBox
from .geometry_data import GeometryData
from .attribute_data import AttributeData
from .geometry_storage import GeometryStorage
from .attribute_storage import AttributeStorage
from .geometry_serializer import GeometrySerializer
from .gis_storage_system import GisStorageSystem


class OGRFormatConverter:
    """OGR格式转换器，与C++版本兼容"""

    def __init__(self, input_file: str, output_dir: str):
        self.input_file = input_file
        self.output_dir = output_dir

        # 提取文件名（不含扩展名）作为前缀
        self.base_name = os.path.splitext(os.path.basename(input_file))[0]

        # 创建输出目录
        os.makedirs(output_dir, exist_ok=True)

        # 初始化存储管理器
        self.geometry_storage: Optional[GeometryStorage] = None
        self.attribute_storage: Optional[AttributeStorage] = None

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

        # 空间范围统计
        self.spatial_extent = BBox(
            float("inf"), float("inf"), float("-inf"), float("-inf")
        )

        # 加载的数据
        self.loaded_geometries: Dict[int, GeometryData] = {}
        self.loaded_attributes: Dict[int, AttributeData] = {}
        self.feature_ids: List[int] = []

        # 元数据
        self.metadata: Dict[str, Any] = {}

    def convert_shapefile_to_custom_format(self) -> bool:
        """转换Shapefile到自定义格式，与C++版本完全一致"""
        print("开始转换Shapefile到自定义格式...")
        start_time = time.time()

        try:
            # 打开Shapefile
            driver = ogr.GetDriverByName("ESRI Shapefile")
            dataset = driver.Open(self.input_file, 0)
            if not dataset:
                print(f"无法打开Shapefile: {self.input_file}")
                return False

            layer = dataset.GetLayer()
            if not layer:
                print("无法获取图层")
                return False

            # 初始化存储管理器
            self._initialize_storage()

            # 获取图层信息
            feature_count = layer.GetFeatureCount()
            print(f"找到 {feature_count} 个要素")

            # 转换每个要素
            feature_id = 0
            valid_count = 0

            # 用于跟踪FID映射，避免冲突
            used_fids = set()
            next_available_fid = 1

            for feature in layer:
                try:
                    # 获取原始FID并处理FID冲突
                    original_fid = feature.GetFID()
                    fid = original_fid

                    # 处理FID为0或重复的情况
                    if fid == 0 or fid in used_fids:
                        # 找到下一个可用的FID
                        while next_available_fid in used_fids:
                            next_available_fid += 1
                        fid = next_available_fid
                        next_available_fid += 1

                        if original_fid == 0:
                            print(f"警告: FID为0的要素映射为 {fid}")
                        else:
                            print(f"警告: 重复FID {original_fid} 映射为 {fid}")

                    # 记录使用的FID
                    used_fids.add(fid)

                    # 转换几何数据
                    geometry = self._convert_geometry(feature, fid)
                    if geometry:
                        self.geometry_storage.write_geometry(geometry)
                        # 更新空间范围
                        self._update_spatial_extent(geometry.get_bbox())
                        valid_count += 1

                    # 转换属性数据
                    attribute = self._convert_attributes(feature, fid)
                    if attribute:
                        self.attribute_storage.write_attribute(attribute)

                    feature_id += 1

                    # 显示进度
                    if feature_id % 1000 == 0:
                        print(f"已处理 {feature_id}/{feature_count} 个要素")

                except Exception as e:
                    print(f"转换要素 {feature_id} 时出错: {e}")
                    continue

            # 保存字符串池
            self.attribute_storage.save_string_pool()

            # 构建并保存分块索引
            print("构建几何数据分块索引...")
            self.geometry_storage.build_chunked_index()
            geom_index_file = os.path.join(
                self.output_dir, f"{self.base_name}.geom.chunked_idx"
            )
            self.geometry_storage.save_chunked_index(geom_index_file)

            print("构建属性数据分块索引...")
            self.attribute_storage.build_chunked_index()
            attr_index_file = os.path.join(
                self.output_dir, f"{self.base_name}.attr.chunked_idx"
            )
            self.attribute_storage.save_chunked_index(attr_index_file)

            # 更新统计信息
            self.stats["total_features"] = feature_id
            self.stats["valid_features"] = valid_count
            self.stats["conversion_time_seconds"] = time.time() - start_time

            # 获取字段定义信息
            field_info = self._get_field_definitions(layer)

            # 获取坐标系统信息
            source_crs, target_crs = self._get_coordinate_system_info(layer)

            # 保存元数据
            self._save_metadata(field_info, source_crs, target_crs)

            print(f"转换完成: {valid_count}/{feature_id} 个有效要素")
            print(f"转换时间: {self.stats['conversion_time_seconds']:.2f} 秒")

            return True

        except Exception as e:
            print(f"转换过程中出错: {e}")
            return False
        finally:
            if "dataset" in locals():
                dataset = None

    def load_custom_format_data(self) -> bool:
        """加载自定义格式数据，与C++版本完全一致"""
        print("加载自定义格式数据...")

        try:
            # 清理之前的数据
            self.loaded_geometries.clear()
            self.loaded_attributes.clear()
            self.feature_ids.clear()

            # 加载元数据
            self.metadata = self._load_metadata()
            if not self.metadata:
                print("无法加载元数据")
                return False

            # 从元数据获取要素总数
            total_features = self.metadata.get("total_features", 0)
            if total_features == 0:
                print("元数据中缺少total_features信息")
                return False

            # 生成要素ID列表（从0到total_features-1）
            self.feature_ids = list(range(total_features))

            print(f"找到 {len(self.feature_ids)} 个要素")

            # 初始化存储管理器
            self._initialize_storage()

            # 批量加载几何和属性数据
            for fid in self.feature_ids:
                try:
                    geometry = self.geometry_storage.read_geometry(fid)
                    if geometry:
                        self.loaded_geometries[fid] = geometry

                    attribute = self.attribute_storage.read_attribute(fid)
                    if attribute:
                        self.loaded_attributes[fid] = attribute
                except Exception as e:
                    print(f"加载要素 {fid} 时出错: {e}")
                    continue

            print(f"成功加载 {len(self.loaded_geometries)} 个几何要素")
            print(f"成功加载 {len(self.loaded_attributes)} 个属性要素")

            return len(self.loaded_geometries) > 0 and len(self.loaded_attributes) > 0

        except Exception as e:
            print(f"加载自定义格式数据时出错: {e}")
            return False

    def get_conversion_stats(self) -> Dict[str, Any]:
        """获取转换统计信息，与C++版本完全一致"""
        return self.stats.copy()

    def get_loaded_data_count(self) -> Dict[str, int]:
        """获取已加载数据数量，与C++版本完全一致"""
        return {
            "geometries": len(self.loaded_geometries),
            "attributes": len(self.loaded_attributes),
            "feature_ids": len(self.feature_ids),
        }

    def _initialize_storage(self) -> None:
        """初始化存储管理器，与C++版本完全一致"""
        if not self.geometry_storage:
            geom_file = os.path.join(self.output_dir, f"{self.base_name}.geom")
            self.geometry_storage = GeometryStorage(geom_file)
            self.geometry_storage.set_chunk_size(10000)
            self.geometry_storage.set_cache_size(1000)

        if not self.attribute_storage:
            attr_file = os.path.join(self.output_dir, f"{self.base_name}.attr")
            pool_file = os.path.join(self.output_dir, f"{self.base_name}.pool")
            self.attribute_storage = AttributeStorage(attr_file, pool_file)
            self.attribute_storage.set_chunk_size(10000)
            self.attribute_storage.set_cache_size(1000)
            self.attribute_storage.set_use_mmap_mode(True)

    def _convert_geometry(self, feature, feature_id: int) -> Optional[GeometryData]:
        """转换几何数据，与C++版本完全一致"""
        try:
            geometry = feature.GetGeometryRef()
            if not geometry:
                return None

            # 获取几何类型
            geom_type = self._get_geometry_type(geometry.GetGeometryType())

            # 获取坐标
            coordinates = self._extract_coordinates(geometry)
            if not coordinates:
                return None

            # 计算边界框
            bbox = GeometrySerializer.calculate_bbox(coordinates)

            # 编码坐标
            encoded_coords = GeometrySerializer.encode_coordinates_delta(coordinates)

            # 计算环数量（对于多边形）
            num_rings = 0
            if geom_type == GeometryType.POLYGON:
                num_rings = geometry.GetGeometryCount()

            return GeometryData(feature_id, geom_type, encoded_coords, bbox, num_rings)

        except Exception as e:
            print(f"转换几何数据时出错: {e}")
            return None

    def _convert_attributes(self, feature, feature_id: int) -> Optional[AttributeData]:
        """转换属性数据，与C++版本完全一致"""
        try:
            properties = {}

            # 获取所有属性字段（与C++版本一致，只处理非空字段）
            for i in range(feature.GetFieldCount()):
                field_def = feature.GetFieldDefnRef(i)
                field_name = field_def.GetName()

                # 只处理已设置且非空的字段（与C++版本一致）
                if feature.IsFieldSetAndNotNull(i):
                    field_type = field_def.GetType()
                    if field_type == ogr.OFTInteger:
                        field_value = str(feature.GetFieldAsInteger(i))
                    elif field_type == ogr.OFTInteger64:
                        field_value = str(feature.GetFieldAsInteger64(i))
                    elif field_type == ogr.OFTReal:
                        # 与C++版本一致：使用6位小数精度
                        field_value = f"{feature.GetFieldAsDouble(i):.6f}"
                    elif field_type == ogr.OFTString:
                        field_value = feature.GetFieldAsString(i)
                    elif field_type in [ogr.OFTDate, ogr.OFTTime, ogr.OFTDateTime]:
                        field_value = feature.GetFieldAsString(i)
                    else:
                        field_value = feature.GetFieldAsString(i)
                    properties[field_name] = field_value

            # 与C++版本一致：按字母顺序排序属性（C++的std::map是有序的）
            sorted_properties = dict(sorted(properties.items()))
            return AttributeData(feature_id, sorted_properties)

        except Exception as e:
            print(f"转换属性数据时出错: {e}")
            return None

    def _get_geometry_type(self, ogr_geometry_type: int) -> GeometryType:
        """将OGR几何类型转换为内部几何类型枚举，与C++版本完全一致"""
        if ogr_geometry_type == ogr.wkbPoint or ogr_geometry_type == ogr.wkbPoint25D:
            return GeometryType.POINT
        elif (
            ogr_geometry_type == ogr.wkbLineString
            or ogr_geometry_type == ogr.wkbLineString25D
        ):
            return GeometryType.LINE
        elif (
            ogr_geometry_type == ogr.wkbPolygon
            or ogr_geometry_type == ogr.wkbPolygon25D
        ):
            return GeometryType.POLYGON
        elif (
            ogr_geometry_type == ogr.wkbMultiPoint
            or ogr_geometry_type == ogr.wkbMultiPoint25D
        ):
            return GeometryType.MULTIPOINT
        elif (
            ogr_geometry_type == ogr.wkbMultiLineString
            or ogr_geometry_type == ogr.wkbMultiLineString25D
        ):
            return GeometryType.MULTILINE
        elif (
            ogr_geometry_type == ogr.wkbMultiPolygon
            or ogr_geometry_type == ogr.wkbMultiPolygon25D
        ):
            return GeometryType.MULTIPOLYGON
        else:
            return GeometryType.POINT  # 默认类型

    def _extract_coordinates(self, geometry) -> List[Coordinate]:
        """提取坐标，与C++版本完全一致"""
        coordinates = []

        try:
            if geometry.GetGeometryType() in [ogr.wkbPoint, ogr.wkbPoint25D]:
                # 点
                x, y = geometry.GetX(), geometry.GetY()
                coordinates.append(Coordinate(x, y))

            elif geometry.GetGeometryType() in [
                ogr.wkbLineString,
                ogr.wkbLineString25D,
            ]:
                # 线
                point_count = geometry.GetPointCount()
                for i in range(point_count):
                    x, y = geometry.GetX(i), geometry.GetY(i)
                    coordinates.append(Coordinate(x, y))

            elif geometry.GetGeometryType() in [ogr.wkbPolygon, ogr.wkbPolygon25D]:
                # 多边形 - 只提取外环
                ring = geometry.GetGeometryRef(0)  # 外环
                if ring:
                    point_count = ring.GetPointCount()
                    for i in range(point_count):
                        x, y = ring.GetX(i), ring.GetY(i)
                        coordinates.append(Coordinate(x, y))

            elif geometry.GetGeometryType() in [
                ogr.wkbMultiPoint,
                ogr.wkbMultiPoint25D,
            ]:
                # 多点
                for i in range(geometry.GetGeometryCount()):
                    point = geometry.GetGeometryRef(i)
                    if point:
                        x, y = point.GetX(), point.GetY()
                        coordinates.append(Coordinate(x, y))

            elif geometry.GetGeometryType() in [
                ogr.wkbMultiLineString,
                ogr.wkbMultiLineString25D,
            ]:
                # 多线 - 连接所有线
                for i in range(geometry.GetGeometryCount()):
                    line = geometry.GetGeometryRef(i)
                    if line:
                        point_count = line.GetPointCount()
                        for j in range(point_count):
                            x, y = line.GetX(j), line.GetY(j)
                            coordinates.append(Coordinate(x, y))

            elif geometry.GetGeometryType() in [
                ogr.wkbMultiPolygon,
                ogr.wkbMultiPolygon25D,
            ]:
                # 多多边形 - 只提取第一个多边形的外环
                if geometry.GetGeometryCount() > 0:
                    polygon = geometry.GetGeometryRef(0)
                    if polygon and polygon.GetGeometryCount() > 0:
                        ring = polygon.GetGeometryRef(0)  # 外环
                        if ring:
                            point_count = ring.GetPointCount()
                            for i in range(point_count):
                                x, y = ring.GetX(i), ring.GetY(i)
                                coordinates.append(Coordinate(x, y))

        except Exception as e:
            print(f"提取坐标时出错: {e}")

        return coordinates

    def _save_metadata(
        self, field_info: Dict[str, int], source_crs: str, target_crs: str
    ) -> None:
        """保存元数据，与C++版本完全一致"""
        # 创建元数据文件路径
        metadata_file = os.path.join(self.output_dir, f"{self.base_name}_meta.json")

        # 读取现有元数据（如果存在）
        metadata = {}
        if os.path.exists(metadata_file):
            try:
                with open(metadata_file, "r", encoding="utf-8") as f:
                    metadata = json.load(f)
            except Exception as e:
                print(f"读取元数据文件失败: {e}")

        # 更新字段定义
        metadata["field_definitions"] = field_info

        # 更新源文件信息
        metadata["source_file"] = os.path.basename(self.input_file)

        # 获取实际的驱动名称
        source_format = "Unknown"
        try:
            from osgeo import gdal

            dataset = gdal.OpenEx(self.input_file, gdal.OF_VECTOR, None, None, None)
            if dataset:
                driver = dataset.GetDriver()
                if driver:
                    driver_name = driver.GetDescription()
                    if driver_name:
                        source_format = driver_name
                dataset = None  # 释放资源
        except Exception as e:
            print(f"获取驱动信息失败: {e}")
            source_format = "ESRI Shapefile"  # 默认值

        metadata["source_format"] = source_format

        # 更新坐标系统信息
        metadata["source_coordinate_system"] = source_crs
        metadata["target_coordinate_system"] = target_crs

        # 更新空间范围
        spatial_extent = self._calculate_spatial_extent()
        metadata["spatial_extent"] = {
            "min_x": spatial_extent.min_x,
            "min_y": spatial_extent.min_y,
            "max_x": spatial_extent.max_x,
            "max_y": spatial_extent.max_y,
        }

        # 添加创建时间
        metadata["creation_date"] = time.strftime("%Y-%m-%d %H:%M:%S")

        # 添加压缩信息
        if self.attribute_storage:
            compression_stats = self.attribute_storage.get_compression_stats()
            metadata["compression_info"] = {
                "compression_ratio": compression_stats["compression_ratio"],
                "original_size": compression_stats["original_size"],
                "compressed_size": compression_stats["compressed_size"],
                "unique_strings": compression_stats["unique_strings"],
                "total_strings": compression_stats["total_strings"],
                "saved_bytes": compression_stats["original_size"]
                - compression_stats["compressed_size"],
            }

        # 添加基本统计信息
        metadata["total_features"] = self.stats["total_features"]
        metadata["valid_features"] = self.stats["valid_features"]
        metadata["conversion_time_seconds"] = self.stats["conversion_time_seconds"]

        # 保存元数据
        try:
            with open(metadata_file, "w", encoding="utf-8") as f:
                json.dump(metadata, f, indent=4, ensure_ascii=False)
            print(f"元数据已保存到文件: {metadata_file}")
            print(f"  源格式: {source_format}")
            print(f"  字段定义: {len(field_info)} 个字段")
            print(f"  源坐标系统: {source_crs}")
            print(f"  目标坐标系统: {target_crs}")
            print(
                f"  空间范围: [{spatial_extent.min_x:.6f}, {spatial_extent.min_y:.6f} - {spatial_extent.max_x:.6f}, {spatial_extent.max_y:.6f}]"
            )
            print(f"  创建时间: {metadata['creation_date']}")
            if self.attribute_storage:
                compression_stats = self.attribute_storage.get_compression_stats()
                print(f"  压缩率: {compression_stats['compression_ratio']:.2f}%")
        except IOError as e:
            print(f"无法保存元数据文件: {metadata_file} (错误: {e})")

    def _get_field_definitions(self, layer) -> Dict[str, int]:
        """获取字段定义信息，与C++版本完全一致"""
        field_info = {}
        layer_defn = layer.GetLayerDefn()

        for i in range(layer_defn.GetFieldCount()):
            field_defn = layer_defn.GetFieldDefn(i)
            field_name = field_defn.GetName()
            field_type = field_defn.GetType()

            # 根据OGR字段类型映射到字节大小
            if field_type == ogr.OFTInteger:
                field_info[field_name] = 4
            elif field_type == ogr.OFTInteger64:
                field_info[field_name] = 8
            elif field_type == ogr.OFTReal:
                field_info[field_name] = 8
            elif field_type == ogr.OFTString:
                field_info[field_name] = 4  # 字符串池ID
            elif field_type == ogr.OFTDate:
                field_info[field_name] = 4
            elif field_type == ogr.OFTTime:
                field_info[field_name] = 4
            elif field_type == ogr.OFTDateTime:
                field_info[field_name] = 8
            else:
                field_info[field_name] = 4  # 默认值

        return field_info

    def _get_coordinate_system_info(self, layer) -> Tuple[str, str]:
        """获取坐标系统信息，与C++版本完全一致"""
        try:
            spatial_ref = layer.GetSpatialRef()
            if spatial_ref:
                # 获取坐标系统名称
                crs_name = spatial_ref.GetName()
                if not crs_name:
                    crs_name = "Unknown"
                return crs_name, crs_name
            else:
                return "Unknown", "Unknown"
        except Exception as e:
            print(f"获取坐标系统信息失败: {e}")
            return "Unknown", "Unknown"

    def _load_metadata(self) -> Dict[str, Any]:
        """加载元数据，与C++版本完全一致"""
        metadata_file = os.path.join(self.output_dir, f"{self.base_name}_meta.json")

        if not os.path.exists(metadata_file):
            return {}

        try:
            with open(metadata_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            print(f"加载元数据失败: {e}")
            return {}

    def _calculate_file_checksum(self, file_path: str) -> str:
        """计算文件校验和，与C++版本完全一致"""
        import hashlib

        try:
            with open(file_path, "rb") as f:
                file_hash = hashlib.md5()
                while chunk := f.read(8192):
                    file_hash.update(chunk)
                return file_hash.hexdigest()
        except IOError:
            return ""

    def _update_spatial_extent(self, bbox: BBox) -> None:
        """更新空间范围，与C++版本完全一致"""
        if bbox.is_valid():
            self.spatial_extent = BBox(
                min(self.spatial_extent.min_x, bbox.min_x),
                min(self.spatial_extent.min_y, bbox.min_y),
                max(self.spatial_extent.max_x, bbox.max_x),
                max(self.spatial_extent.max_y, bbox.max_y),
            )

    def _calculate_spatial_extent(self) -> BBox:
        """计算空间范围，与C++版本完全一致"""
        # 如果没有有效的空间范围，返回默认值
        if not self.spatial_extent.is_valid():
            return BBox(0.0, 0.0, 0.0, 0.0)
        return self.spatial_extent
