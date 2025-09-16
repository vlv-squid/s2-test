# gissystem.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import os
import json
import hashlib
from datetime import datetime
from typing import Tuple, List, Dict, Optional, Any
from gisstorage.converter import ShapefileConverter
from gisstorage.storage import GeometryStorage, AttributeStorage


class GisStorageSystem:
    """GIS存储系统主类 - 集成文件扩展名定义、S2索引和元数据管理"""

    # 文件扩展名定义 - 统一管理所有文件格式
    class FileExtensions:
        GEOMETRY_DATA = ".geom"  # 几何数据文件
        ATTRIBUTE_DATA = ".attr"  # 属性数据文件
        STRING_POOL = ".pool"  # 字符串池文件
        INDEX_DATA = ".idx"  # 索引数据文件
        METADATA = "_meta.json"  # 元数据文件
        S2_INDEX = ".s2idx"  # S2空间索引文件

    def __init__(self, output_dir: str):
        self.output_dir = output_dir
        self.shapefile_name = ""
        
        # 创建输出目录
        os.makedirs(output_dir, exist_ok=True)
        
        # 初始化存储对象
        self.geometry_storage: Optional[GeometryStorage] = None
        self.attribute_storage: Optional[AttributeStorage] = None
        
        # 文件路径
        self.geom_file = ""
        self.attr_file = ""
        self.pool_file = ""
        self.index_file = ""
        self.metadata_file = ""
        self.s2_index_file = ""
        
        # 元数据
        self.metadata = {
            "format_version": "1.0",
            "source_format": "Shapefile",
            "source_file": "",
            "creation_date": self._get_current_timestamp(),
            "source_coordinate_system": "",
            "target_coordinate_system": "",
            "spatial_extent": {"min_x": 0.0, "min_y": 0.0, "max_x": 0.0, "max_y": 0.0},
            "total_features": 0,
            "valid_features": 0,
            "field_definitions": {},
            "file_sizes": {},
            "checksums": {},
            "compression_info": "",
            "s2_index_info": ""
        }

    def initialize_storage_files(self, shapefile_path: str):
        """初始化存储文件"""
        # 提取Shapefile名称
        self.shapefile_name = os.path.splitext(os.path.basename(shapefile_path))[0]
        
        # 初始化文件路径
        self._initialize_file_paths()
        
        # 初始化存储对象
        self.geometry_storage = GeometryStorage(self.geom_file)
        self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
        
        # 创建元数据
        self._create_metadata_from_shapefile(shapefile_path)
        
        # 加载索引
        if os.path.exists(self.index_file):
            self.geometry_storage.load_index_from_file(self.index_file)
            self.attribute_storage.load_index_from_file(self.index_file)
        
        # 加载元数据
        if os.path.exists(self.metadata_file):
            self._load_metadata()
        
        # 更新文件统计信息
        self._update_file_sizes()
        self._update_checksums()

    def set_dataset_name(self, dataset_name: str):
        """设置数据集名称（用于加载现有数据）"""
        self.shapefile_name = dataset_name
        
        # 重新初始化文件路径
        self._initialize_file_paths()
        
        # 重新初始化存储对象
        self.geometry_storage = GeometryStorage(self.geom_file)
        self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
        
        # 加载索引
        if os.path.exists(self.index_file):
            self.geometry_storage.load_index_from_file(self.index_file)
            self.attribute_storage.load_index_from_file(self.index_file)
        
        # 加载元数据
        if os.path.exists(self.metadata_file):
            self._load_metadata()
        
        # 更新文件统计信息
        self._update_file_sizes()
        self._update_checksums()

    def set_dataset_name_lightweight(self, dataset_name: str):
        """轻量级设置数据集名称（仅加载S2索引和元数据，不加载几何和属性索引）"""
        self.shapefile_name = dataset_name
        
        # 重新初始化文件路径
        self._initialize_file_paths()
        
        # 轻量级模式：不初始化几何和属性存储对象
        # 这些对象将在需要时按需创建
        
        # 只加载元数据
        if os.path.exists(self.metadata_file):
            self._load_metadata()
        
        # 更新文件统计信息
        self._update_file_sizes()
        self._update_checksums()

    def _initialize_file_paths(self):
        """初始化文件路径"""
        self.geom_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.GEOMETRY_DATA)
        self.attr_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.ATTRIBUTE_DATA)
        self.pool_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.STRING_POOL)
        self.index_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.INDEX_DATA)
        self.metadata_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.METADATA)
        self.s2_index_file = os.path.join(self.output_dir, self.shapefile_name + self.FileExtensions.S2_INDEX)

    def _create_metadata_from_shapefile(self, shapefile_path: str):
        """从Shapefile创建元数据"""
        self.metadata["source_file"] = os.path.basename(shapefile_path)
        self.metadata["source_format"] = "Shapefile"
        self.metadata["creation_date"] = self._get_current_timestamp()

    def _load_metadata(self):
        """加载元数据"""
        if not os.path.exists(self.metadata_file):
            return
        
        try:
            with open(self.metadata_file, 'r', encoding='utf-8') as f:
                loaded_metadata = json.load(f)
            
            # 更新元数据
            self.metadata.update(loaded_metadata)
        except Exception as e:
            print(f"加载元数据失败: {e}")

    def _save_metadata(self):
        """保存元数据"""
        try:
            with open(self.metadata_file, 'w', encoding='utf-8') as f:
                json.dump(self.metadata, f, indent=4, ensure_ascii=False)
        except Exception as e:
            print(f"保存元数据失败: {e}")

    def _update_file_sizes(self):
        """更新文件大小信息"""
        self.metadata["file_sizes"] = {}
        
        files_to_check = [
            ("geometry", self.geom_file),
            ("attribute", self.attr_file),
            ("string_pool", self.pool_file),
            ("index", self.index_file),
            ("s2_index", self.s2_index_file)
        ]
        
        for file_type, file_path in files_to_check:
            if os.path.exists(file_path):
                self.metadata["file_sizes"][file_type] = os.path.getsize(file_path)

    def _update_checksums(self):
        """更新文件校验和"""
        self.metadata["checksums"] = {}
        
        files_to_check = [
            ("geometry", self.geom_file),
            ("attribute", self.attr_file),
            ("string_pool", self.pool_file),
            ("index", self.index_file),
            ("s2_index", self.s2_index_file)
        ]
        
        for file_type, file_path in files_to_check:
            if os.path.exists(file_path):
                self.metadata["checksums"][file_type] = self._calculate_file_checksum(file_path)

    def _calculate_file_checksum(self, file_path: str) -> str:
        """计算文件校验和"""
        try:
            with open(file_path, 'rb') as f:
                content = f.read()
            return hashlib.md5(content).hexdigest()
        except Exception:
            return ""

    def _get_current_timestamp(self) -> str:
        """获取当前时间戳"""
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def get_metadata(self) -> Dict[str, Any]:
        """获取元数据"""
        return self.metadata.copy()

    def get_storage_stats(self) -> Dict[str, Any]:
        """获取存储统计信息"""
        stats = {
            "total_files": 0,
            "total_size_bytes": 0,
            "geometry_size_bytes": 0,
            "attribute_size_bytes": 0,
            "string_pool_size_bytes": 0,
            "index_size_bytes": 0,
            "s2_index_size_bytes": 0,
            "compression_ratio": 0.0,
            "string_pool_saved_bytes": 0
        }
        
        # 只统计5个核心文件，不包含元数据文件
        core_files = ["geometry", "attribute", "string_pool", "index", "s2_index"]
        
        for file_type in core_files:
            if file_type in self.metadata["file_sizes"]:
                size = self.metadata["file_sizes"][file_type]
                stats["total_files"] += 1
                stats["total_size_bytes"] += size
                
                if file_type == "geometry":
                    stats["geometry_size_bytes"] = size
                elif file_type == "attribute":
                    stats["attribute_size_bytes"] = size
                elif file_type == "string_pool":
                    stats["string_pool_size_bytes"] = size
                elif file_type == "index":
                    stats["index_size_bytes"] = size
                elif file_type == "s2_index":
                    stats["s2_index_size_bytes"] = size
        
        return stats

    def convert_shapefile(self, shapefile_path: str):
        """转换Shapefile"""
        converter = ShapefileConverter(shapefile_path, self.output_dir)
        return converter.convert()

    # 读取方法
    def read_geometry(self, feature_id: int):
        """读取几何数据"""
        if not self.geometry_storage:
            raise RuntimeError("几何存储未初始化")
        return self.geometry_storage.read_geometry(feature_id)

    def read_attribute(self, feature_id: int):
        """读取属性数据"""
        if not self.attribute_storage:
            raise RuntimeError("属性存储未初始化")
        return self.attribute_storage.read_attribute(feature_id)

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        if not self.geometry_storage:
            return []
        return self.geometry_storage.get_all_feature_ids()

    def read_geometries(self, feature_ids: List[int]) -> Dict[int, Any]:
        """批量读取几何数据"""
        results = {}
        if not self.geometry_storage:
            return results

        for fid in feature_ids:
            try:
                geom = self.geometry_storage.read_geometry(fid)
                if geom:
                    results[fid] = geom
            except Exception as e:
                print(f"读取几何数据失败 FID {fid}: {e}")

        return results

    def read_attributes(self, feature_ids: List[int]) -> Dict[int, Any]:
        """批量读取属性数据"""
        results = {}
        if not self.attribute_storage:
            return results

        for fid in feature_ids:
            try:
                attr = self.attribute_storage.read_attribute(fid)
                if attr:
                    results[fid] = attr
            except Exception as e:
                print(f"读取属性数据失败 FID {fid}: {e}")

        return results

    # 属性查询方法
    def query_by_attribute_efficient(self, field_name: str, field_value: str) -> List[int]:
        """高效的属性查询方法（基于字符串池）"""
        results = []

        # 如果属性存储未初始化，尝试按需初始化
        if not self.attribute_storage:
            try:
                # 重新初始化文件路径
                self._initialize_file_paths()
                # 初始化属性存储对象
                self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
                # 加载索引
                if os.path.exists(self.index_file):
                    self.attribute_storage.load_index_from_file(self.index_file)
            except Exception as e:
                print(f"属性存储初始化失败: {e}")
                return results

        try:
            print(f"      基于字符串池的高效属性查询：字段='{field_name}', 值='{field_value}'")

            # 获取字符串池的压缩统计信息
            stats = self.attribute_storage.get_compression_stats()
            print(f"      字符串池包含 {stats['unique_strings']} 个唯一字符串")

            # 获取总要素数
            total_features = self.metadata.get("total_features", 0)
            print(f"      将查询 {total_features} 个要素（批量读取优化）")

            # 批量读取优化：每次处理100000个要素
            batch_size = 100000
            processed = 0
            found_count = 0

            for i in range(1, total_features + 1, batch_size):
                end_fid = min(i + batch_size - 1, total_features)
                batch_ids = list(range(i, end_fid + 1))

                # 批量读取属性
                batch_attrs = self.read_attributes(batch_ids)

                # 处理批量结果
                for fid, attr in batch_attrs.items():
                    if attr:
                        value = attr.properties.get(field_name, "")
                        if value == field_value:
                            results.append(fid)
                            found_count += 1

                processed += len(batch_ids)

                # 每处理10万个要素显示一次进度
                if processed % 100000 == 0:
                    print(f"\r      进度: {processed}/{total_features} ({100.0 * processed / total_features:.1f}%) "
                          f"找到: {found_count}", end="", flush=True)

            print(f"      属性查询完成，找到 {len(results)} 个匹配要素")

        except Exception as e:
            print(f"属性查询错误: {e}")

        return results

    def query_by_attribute_pattern(self, field_name: str, pattern: str) -> List[int]:
        """属性模式查询方法"""
        results = []

        # 如果属性存储未初始化，尝试按需初始化
        if not self.attribute_storage:
            try:
                self._initialize_file_paths()
                self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
                if os.path.exists(self.index_file):
                    self.attribute_storage.load_index_from_file(self.index_file)
            except Exception as e:
                print(f"属性存储初始化失败: {e}")
                return results

        try:
            # 获取所有要素ID
            all_feature_ids = self.get_all_feature_ids()
            if not all_feature_ids:
                # 轻量级模式：从元数据获取总要素数，生成FID列表
                total_features = self.metadata.get("total_features", 0)
                all_feature_ids = list(range(1, total_features + 1))

            # 遍历所有要素，查找匹配的属性模式
            for fid in all_feature_ids:
                try:
                    attr = self.attribute_storage.read_attribute(fid)
                    if attr:
                        value = attr.properties.get(field_name, "")
                        # 简单的包含匹配（可以扩展为正则表达式）
                        if pattern in value:
                            results.append(fid)
                except Exception:
                    # 忽略单个要素读取错误，继续处理其他要素
                    continue

        except Exception as e:
            print(f"属性模式查询错误: {e}")

        return results

    def query_attribute_values(self, field_name: str) -> List[Tuple[int, str]]:
        """查询属性值"""
        results = []

        # 如果属性存储未初始化，尝试按需初始化
        if not self.attribute_storage:
            try:
                self._initialize_file_paths()
                self.attribute_storage = AttributeStorage(self.attr_file, self.pool_file)
                if os.path.exists(self.index_file):
                    self.attribute_storage.load_index_from_file(self.index_file)
            except Exception as e:
                print(f"属性存储初始化失败: {e}")
                return results

        try:
            # 获取所有要素ID
            all_feature_ids = self.get_all_feature_ids()
            if not all_feature_ids:
                # 轻量级模式：从元数据获取总要素数，生成FID列表
                total_features = self.metadata.get("total_features", 0)
                all_feature_ids = list(range(1, total_features + 1))

            # 遍历所有要素，获取指定字段的值
            for fid in all_feature_ids:
                try:
                    attr = self.attribute_storage.read_attribute(fid)
                    if attr:
                        value = attr.properties.get(field_name, "")
                        if value:
                            results.append((fid, value))
                except Exception:
                    # 忽略单个要素读取错误，继续处理其他要素
                    continue

        except Exception as e:
            print(f"属性值查询错误: {e}")

        return results

    # 空间查询方法（需要S2索引支持）
    def query_s2_index(self, bbox: Tuple[float, float, float, float], resolution: int = 15) -> List[int]:
        """S2空间索引查询"""
        # 这里需要S2索引的实现，暂时返回空列表
        print("S2空间索引查询功能需要S2索引支持")
        return []

    def query_spatial_attribute_efficient(self, bbox: Tuple[float, float, float, float], 
                                        field_name: str, field_value: str) -> List[int]:
        """高效的复合查询方法（空间+属性）"""
        results = []

        try:
            print(f"      高效复合查询：空间范围={bbox}, 字段='{field_name}', 值='{field_value}'")

            # 第一步：空间查询获取候选要素ID
            spatial_results = self.query_s2_index(bbox)
            print(f"      空间查询找到 {len(spatial_results)} 个候选要素")

            if not spatial_results:
                print("      空间查询无结果，复合查询完成")
                return results

            # 第二步：对空间查询结果进行批量属性查询
            batch_size = 10000  # 批量处理空间查询结果
            processed = 0
            found_count = 0

            for i in range(0, len(spatial_results), batch_size):
                end_idx = min(i + batch_size, len(spatial_results))
                batch_ids = spatial_results[i:end_idx]

                # 批量读取属性
                batch_attrs = self.read_attributes(batch_ids)

                # 处理批量结果
                for fid, attr in batch_attrs.items():
                    if attr:
                        value = attr.properties.get(field_name, "")
                        if value == field_value:
                            results.append(fid)
                            found_count += 1

                processed += len(batch_ids)

                # 每处理1万个要素显示一次进度
                if processed % 50000 == 0 or processed == len(spatial_results):
                    print(f"\r      属性过滤进度: {processed}/{len(spatial_results)} "
                          f"({100.0 * processed / len(spatial_results):.1f}%) "
                          f"找到: {found_count}", end="", flush=True)

            print(f"      复合查询完成，从 {len(spatial_results)} 个空间候选要素中找到 {len(results)} 个匹配要素")

        except Exception as e:
            print(f"复合查询错误: {e}")

        return results
