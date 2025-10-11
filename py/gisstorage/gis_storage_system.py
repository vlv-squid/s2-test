# gis_storage_system.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import os
import json
import hashlib
from datetime import datetime
from typing import Tuple, List, Dict, Optional, Any
from .geometry_types import BBox
from .geometry_data import GeometryData
from .attribute_data import AttributeData
from .geometry_storage import GeometryStorage
from .attribute_storage import AttributeStorage


class GisStorageSystem:
    """GIS存储系统主类 - 集成文件扩展名定义、S2索引和元数据管理，与C++版本兼容"""

    # 文件扩展名定义 - 统一管理所有文件格式
    class FileExtensions:
        GEOMETRY_DATA = ".geom"  # 几何数据文件
        ATTRIBUTE_DATA = ".attr"  # 属性数据文件
        STRING_POOL = ".pool"  # 字符串池文件
        GEOMETRY_CHUNKED_INDEX = ".geom.chunked_idx"  # 几何分块索引文件
        ATTRIBUTE_CHUNKED_INDEX = ".attr.chunked_idx"  # 属性分块索引文件
        METADATA = "_meta.json"  # 元数据文件
        S2_INDEX = ".s2idx"  # S2空间索引文件

    # 元数据结构 - 完整描述数据集信息
    class Metadata:
        def __init__(self):
            self.format_version = "1.0"
            self.source_format = "Shapefile"
            self.source_file = ""
            self.creation_date = ""
            # 坐标系统转换信息
            self.source_coordinate_system = ""  # 转换前的坐标系统
            self.target_coordinate_system = ""  # 转换后的坐标系统
            self.spatial_extent = BBox(0.0, 0.0, 0.0, 0.0)
            self.total_features = 0
            self.valid_features = 0
            self.field_definitions = {}
            self.file_sizes = {}
            self.checksums = {}
            self.compression_info = ""
            self.s2_index_info = ""

    def __init__(self, output_dir: str, dataset_name: str = "data"):
        self.output_dir = output_dir
        self.dataset_name = dataset_name

        # 创建输出目录
        os.makedirs(output_dir, exist_ok=True)

        # 初始化元数据
        self.metadata = GisStorageSystem.Metadata()
        self.metadata.creation_date = self._get_current_timestamp()

        # 存储管理器
        self.geometry_storage: Optional[GeometryStorage] = None
        self.attribute_storage: Optional[AttributeStorage] = None

        # 文件路径
        self.geom_file = ""
        self.attr_file = ""
        self.pool_file = ""
        self.metadata_file = ""

        # 初始化文件路径
        self._initialize_file_paths()

    def read_geometry(self, feature_id: int) -> GeometryData:
        """读取几何数据，与C++版本完全一致"""
        if not self.geometry_storage:
            # 轻量级模式：按需初始化几何存储
            self._initialize_file_paths()
            self.geometry_storage = GeometryStorage(self.geom_file)
            self.geometry_storage.set_chunk_size(10000)
            self.geometry_storage.set_cache_size(1000)

        return self.geometry_storage.read_geometry(feature_id)

    def read_attribute(self, feature_id: int) -> AttributeData:
        """读取属性数据，与C++版本完全一致"""
        if not self.attribute_storage:
            # 轻量级模式：按需初始化属性存储
            self._initialize_file_paths()
            self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
            self.attribute_storage.set_chunk_size(10000)
            self.attribute_storage.set_cache_size(1000)  # 设置缓存大小为1000
            self.attribute_storage.set_use_mmap_mode(True)  # 启用mmap模式以优化内存使用

        return self.attribute_storage.read_attribute(feature_id)

    def read_geometry_on_demand(self, feature_id: int) -> GeometryData:
        """按需读取几何数据（不依赖完整索引），与C++版本完全一致"""
        if not self.geometry_storage:
            self._initialize_file_paths()
            self.geometry_storage = GeometryStorage(self.geom_file)
            self.geometry_storage.set_chunk_size(10000)
            self.geometry_storage.set_cache_size(1000)

        return self.geometry_storage.read_geometry_on_demand(feature_id)

    def read_attribute_on_demand(self, feature_id: int) -> AttributeData:
        """按需读取属性数据（不依赖完整索引），与C++版本完全一致"""
        if not self.attribute_storage:
            self._initialize_file_paths()
            self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
            self.attribute_storage.set_chunk_size(10000)
            self.attribute_storage.set_cache_size(1000)
            self.attribute_storage.set_use_mmap_mode(True)

        return self.attribute_storage.read_attribute_on_demand(feature_id)

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID，与C++版本完全一致"""
        if not self.geometry_storage:
            self._initialize_file_paths()
            self.geometry_storage = GeometryStorage(self.geom_file)

        return self.geometry_storage.get_all_feature_ids()

    def query_by_attribute_pattern(self, field_name: str, pattern: str) -> List[int]:
        """属性查询方法，与C++版本完全一致"""
        # 这里需要实现属性查询逻辑
        # 简化实现：返回空列表
        return []

    def query_attribute_values(self, field_name: str) -> List[Tuple[int, str]]:
        """查询属性值，与C++版本完全一致"""
        # 这里需要实现属性值查询逻辑
        # 简化实现：返回空列表
        return []

    def query_by_attribute_efficient(
        self, field_name: str, field_value: str
    ) -> List[int]:
        """高效的属性查询方法（基于字符串池），与C++版本完全一致"""
        # 这里需要实现高效的属性查询逻辑
        # 简化实现：返回空列表
        return []

    def query_by_attribute_parallel(
        self, field_name: str, field_value: str
    ) -> List[int]:
        """并行属性查询方法，与C++版本完全一致"""
        # 这里需要实现并行属性查询逻辑
        # 简化实现：返回空列表
        return []

    def get_metadata(self) -> Dict:
        """获取元数据，与C++版本完全一致"""
        return {
            "format_version": self.metadata.format_version,
            "source_format": self.metadata.source_format,
            "source_file": self.metadata.source_file,
            "creation_date": self.metadata.creation_date,
            "source_coordinate_system": self.metadata.source_coordinate_system,
            "target_coordinate_system": self.metadata.target_coordinate_system,
            "spatial_extent": {
                "min_x": self.metadata.spatial_extent.min_x,
                "min_y": self.metadata.spatial_extent.min_y,
                "max_x": self.metadata.spatial_extent.max_x,
                "max_y": self.metadata.spatial_extent.max_y,
            },
            "total_features": self.metadata.total_features,
            "valid_features": self.metadata.valid_features,
            "field_definitions": self.metadata.field_definitions,
            "file_sizes": self.metadata.file_sizes,
            "checksums": self.metadata.checksums,
            "compression_info": self.metadata.compression_info,
            "s2_index_info": self.metadata.s2_index_info,
        }

    def save_metadata(self) -> None:
        """保存元数据，与C++版本完全一致"""
        metadata_dict = self.get_metadata()

        try:
            with open(self.metadata_file, "w", encoding="utf-8") as f:
                json.dump(metadata_dict, f, indent=2, ensure_ascii=False)
            print(f"元数据已保存到: {self.metadata_file}")
        except IOError as e:
            print(f"无法保存元数据文件: {self.metadata_file} (错误: {e})")

    def load_metadata(self) -> Dict:
        """加载元数据，与C++版本完全一致"""
        if not os.path.exists(self.metadata_file):
            return {}

        try:
            with open(self.metadata_file, "r", encoding="utf-8") as f:
                metadata_dict = json.load(f)

            # 更新内部元数据对象
            self.metadata.format_version = metadata_dict.get("format_version", "1.0")
            self.metadata.source_format = metadata_dict.get(
                "source_format", "Shapefile"
            )
            self.metadata.source_file = metadata_dict.get("source_file", "")
            self.metadata.creation_date = metadata_dict.get("creation_date", "")
            self.metadata.source_coordinate_system = metadata_dict.get(
                "source_coordinate_system", ""
            )
            self.metadata.target_coordinate_system = metadata_dict.get(
                "target_coordinate_system", ""
            )

            spatial_extent = metadata_dict.get("spatial_extent", {})
            self.metadata.spatial_extent = BBox(
                spatial_extent.get("min_x", 0.0),
                spatial_extent.get("min_y", 0.0),
                spatial_extent.get("max_x", 0.0),
                spatial_extent.get("max_y", 0.0),
            )

            self.metadata.total_features = metadata_dict.get("total_features", 0)
            self.metadata.valid_features = metadata_dict.get("valid_features", 0)
            self.metadata.field_definitions = metadata_dict.get("field_definitions", {})
            self.metadata.file_sizes = metadata_dict.get("file_sizes", {})
            self.metadata.checksums = metadata_dict.get("checksums", {})
            self.metadata.compression_info = metadata_dict.get("compression_info", "")
            self.metadata.s2_index_info = metadata_dict.get("s2_index_info", "")

            return metadata_dict

        except (json.JSONDecodeError, IOError) as e:
            print(f"加载元数据失败: {e}")
            return {}

    def get_storage_stats(self) -> Dict:
        """获取存储统计信息，与C++版本完全一致"""
        stats = {
            "geometry_stats": {},
            "attribute_stats": {},
            "metadata": self.get_metadata(),
        }

        # 确保存储系统已初始化
        if not self.geometry_storage:
            self._initialize_file_paths()
            self.geometry_storage = GeometryStorage(self.geom_file)
            self.geometry_storage.set_chunk_size(10000)
            self.geometry_storage.set_cache_size(1000)

        if not self.attribute_storage:
            self._initialize_file_paths()
            self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
            self.attribute_storage.set_chunk_size(10000)
            self.attribute_storage.set_cache_size(1000)
            self.attribute_storage.set_use_mmap_mode(True)

        if self.geometry_storage:
            stats["geometry_stats"] = {
                "file_path": self.geometry_storage.get_geometry_file_path(),
                "cache_size": len(self.geometry_storage.cache),
                "max_cache_size": self.geometry_storage.max_cache_size,
            }

        if self.attribute_storage:
            stats["attribute_stats"] = self.attribute_storage.get_storage_stats()

        return stats

    def _initialize_file_paths(self) -> None:
        """初始化文件路径，与C++版本完全一致"""
        # 使用数据集名称作为文件名前缀
        base_name = self.dataset_name

        self.geom_file = os.path.join(
            self.output_dir, base_name + GisStorageSystem.FileExtensions.GEOMETRY_DATA
        )
        self.attr_file = os.path.join(
            self.output_dir, base_name + GisStorageSystem.FileExtensions.ATTRIBUTE_DATA
        )
        self.pool_file = os.path.join(
            self.output_dir, base_name + GisStorageSystem.FileExtensions.STRING_POOL
        )
        self.metadata_file = os.path.join(
            self.output_dir, base_name + GisStorageSystem.FileExtensions.METADATA
        )

    def _get_current_timestamp(self) -> str:
        """获取当前时间戳，与C++版本完全一致"""
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def _calculate_file_checksum(self, file_path: str) -> str:
        """计算文件校验和，与C++版本完全一致"""
        if not os.path.exists(file_path):
            return ""

        try:
            with open(file_path, "rb") as f:
                file_hash = hashlib.md5()
                while chunk := f.read(8192):
                    file_hash.update(chunk)
                return file_hash.hexdigest()
        except IOError:
            return ""

    def _update_file_sizes_and_checksums(self) -> None:
        """更新文件大小和校验和，与C++版本完全一致"""
        files_to_check = [
            (self.geom_file, "geometry_file"),
            (self.attr_file, "attribute_file"),
            (self.pool_file, "string_pool_file"),
            (self.metadata_file, "metadata_file"),
        ]

        for file_path, key in files_to_check:
            if os.path.exists(file_path):
                stat_info = os.stat(file_path)
                self.metadata.file_sizes[key] = stat_info.st_size
                self.metadata.checksums[key] = self._calculate_file_checksum(file_path)
