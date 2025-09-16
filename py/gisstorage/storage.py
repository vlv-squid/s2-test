# storage.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import os
import struct
import json
from typing import Dict, List, Tuple, Optional
from gisstorage.serializers import GeometrySerializer, AttributeSerializer
from gisstorage.models import GeometryData, AttributeData, GeometryType


class GeometryStorage:
    """几何数据存储类，与C++版本兼容"""

    @staticmethod
    def calculate_geometry_size(geometry: GeometryData) -> int:
        """计算几何对象序列化后的大小（新格式）"""
        # feature_id(8) + geometry_type(1) + padding(7) + bbox(32) + num_rings(4) + coord_size(4) + coords
        return 8 + 1 + 7 + 32 + 4 + 4 + len(geometry.coordinates)

    @staticmethod
    def get_serialized_size_from_header(data: bytes) -> int:
        """从数据头部获取完整序列化数据的大小（新格式）"""
        if len(data) < 56:  # 新格式最小长度：52字节头部 + 4字节coord_size
            return 0

        # 解析坐标数据长度
        coord_size = struct.unpack("I", data[52:56])[0]
        # 总大小 = 头部(52) + 坐标大小字段(4) + 坐标数据(coord_size)
        return 52 + 4 + coord_size

    def __init__(self, geometry_file: str):
        self.geometry_file = geometry_file
        self._offset_index = None  # 缓存偏移索引
        self._index_built = False  # 标记索引是否已构建
        # 创建目录
        os.makedirs(os.path.dirname(geometry_file), exist_ok=True)

    def write_geometry(self, geometry: GeometryData) -> int:
        with open(self.geometry_file, "ab") as f:
            offset = f.tell()
            # 创建序列化器实例
            serializer = GeometrySerializer()
            geom_binary = serializer.serialize_geometry(geometry)
            # 写入前校验数据完整性
            if len(geom_binary) > 0:
                f.write(geom_binary)
                # 写入后清除索引缓存，因为文件已改变
                self._offset_index = None
                self._index_built = False
                return offset
            else:
                raise ValueError(f"几何数据序列化失败 for FID {geometry.feature_id}")

    def read_geometry(self, feature_id: int) -> GeometryData:
        """根据要素ID读取几何数据"""
        # 使用缓存的偏移索引
        feature_offsets = self._get_offset_index()
        if feature_id not in feature_offsets:
            raise ValueError(f"Feature ID {feature_id} not found")

        offset = feature_offsets[feature_id]
        with open(self.geometry_file, "rb") as f:
            f.seek(offset)
            
            # 读取完整的数据记录（新格式）
            # 先读取前56字节（包含coord_size字段）
            header_data = f.read(56)
            if len(header_data) < 56:
                raise ValueError(f"几何数据不完整 for FID {feature_id}")
            
            # 新格式：52字节头部 + 4字节coord_size
            coord_size = struct.unpack("I", header_data[52:56])[0]
            header_data = header_data[:52]
            
            # 读取坐标数据
            f.seek(offset + 56)  # 跳过52字节头部 + 4字节coord_size
            coord_data = f.read(coord_size)
            if len(coord_data) < coord_size:
                raise ValueError(f"几何坐标数据不完整 for FID {feature_id}")

            # 组合所有数据进行反序列化
            geom_data = header_data + struct.pack("I", coord_size) + coord_data
            
            # 创建序列化器实例
            serializer = GeometrySerializer()
            geometry, _ = serializer.deserialize_geometry(geom_data)
            return geometry

    def _get_offset_index(self) -> Dict[int, int]:
        """获取偏移索引，使用缓存机制"""
        if self._offset_index is None or not self._index_built:
            self._offset_index = self._build_offset_index()
            self._index_built = True
        return self._offset_index

    def _build_offset_index(self) -> Dict[int, int]:
        offsets = {}
        if not os.path.exists(self.geometry_file):
            return offsets

        with open(self.geometry_file, "rb") as f:
            while True:
                current_offset = f.tell()

                # 读取feature_id
                feature_id_data = f.read(8)
                if len(feature_id_data) < 8:
                    break
                feature_id = struct.unpack("Q", feature_id_data)[0]

                # 跳过geometry_type(1) + padding(7) + bbox(32) + num_rings(4)
                f.seek(44, 1)

                # 读取坐标数据大小
                coord_size_data = f.read(4)
                if len(coord_size_data) < 4:
                    break
                coord_size = struct.unpack("I", coord_size_data)[0]

                # 跳过坐标数据
                f.seek(coord_size, 1)

                offsets[feature_id] = current_offset

        return offsets

    def load_index_from_file(self, index_file: str) -> None:
        """从索引文件加载索引"""
        if not os.path.exists(index_file):
            print(f"索引文件不存在: {index_file}")
            return

        try:
            with open(index_file, "r") as f:
                index_data = json.load(f)

            # 验证JSON结构
            if not index_data.get("version") or not index_data.get("data", {}).get(
                "features"
            ):
                print("索引文件格式不正确")
                return

            print(f"索引文件格式: JSON")
            print(f"版本: {index_data['version']}")
            print(f"要素数量: {len(index_data['data']['features'])}")

            # 构建偏移索引
            self._offset_index = {}
            count = 0
            for fid_str, feature_data in index_data["data"]["features"].items():
                fid = int(fid_str)
                geom_offset = feature_data.get("geom_offset", 0)
                self._offset_index[fid] = geom_offset

                if count < 5:  # 只显示前5个条目
                    print(f"索引条目 {count}: FID={fid}, geom_offset={geom_offset}")
                count += 1

            print(f"加载的索引条目数量: {len(self._offset_index)}")
            self._index_built = True

        except json.JSONDecodeError as e:
            print(f"JSON索引文件解析失败: {e}")

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        offsets = self._get_offset_index()
        return list(offsets.keys())

    def has_feature(self, feature_id: int) -> bool:
        """检查要素是否存在"""
        offsets = self._get_offset_index()
        return feature_id in offsets

    def clear_cache(self) -> None:
        """清除缓存"""
        self._offset_index = None
        self._index_built = False

    def get_geometry_file_path(self) -> str:
        """获取几何文件路径"""
        return self.geometry_file


class AttributeStorage:
    """属性数据存储类，与C++版本兼容，支持字符串池"""

    def __init__(self, attribute_file: str, string_pool_file: str):
        self.attribute_file = attribute_file
        self.string_pool_file = string_pool_file
        self._offset_index = None
        self._index_built = False
        self.serializer = AttributeSerializer()

        # 创建目录
        os.makedirs(os.path.dirname(attribute_file), exist_ok=True)

        # 不自动加载字符串池，由调用者显式调用

    def write_attribute(self, attribute: AttributeData) -> int:
        """写入属性数据"""
        with open(self.attribute_file, "ab") as f:
            offset = f.tell()
            attr_binary = self.serializer.serialize_attributes(attribute)

            if len(attr_binary) > 0:
                f.write(attr_binary)
                # 清除索引缓存
                self._offset_index = None
                self._index_built = False
                return offset
            else:
                raise ValueError(f"属性数据序列化失败 for FID {attribute.feature_id}")

    def read_attribute(self, feature_id: int) -> AttributeData:
        """读取属性数据"""
        offsets = self._get_offset_index()
        if feature_id not in offsets:
            raise ValueError(f"Feature ID {feature_id} not found")

        offset = offsets[feature_id]
        with open(self.attribute_file, "rb") as f:
            f.seek(offset)

            # 读取feature_id和属性数量
            header_data = f.read(12)  # 8 + 4
            if len(header_data) < 12:
                raise ValueError(f"属性数据不完整 for FID {feature_id}")

            feature_id_read, prop_count = struct.unpack("QI", header_data)

            # 读取剩余数据 (key_id + value_id pairs)
            remaining_data = f.read(prop_count * 8)
            if len(remaining_data) < prop_count * 8:
                raise ValueError(f"属性数据不完整 for FID {feature_id}")

            # 组合所有数据进行反序列化
            attr_data = header_data + remaining_data
            attribute, _ = self.serializer.deserialize_attributes(attr_data)
            return attribute

    def _get_offset_index(self) -> Dict[int, int]:
        """获取偏移索引，使用缓存机制"""
        if self._offset_index is None or not self._index_built:
            self._offset_index = self._build_offset_index()
            self._index_built = True
        return self._offset_index

    def _build_offset_index(self) -> Dict[int, int]:
        """构建偏移索引"""
        offsets = {}
        if not os.path.exists(self.attribute_file):
            return offsets

        with open(self.attribute_file, "rb") as f:
            while True:
                current_offset = f.tell()

                # 读取feature_id
                feature_id_data = f.read(8)
                if len(feature_id_data) < 8:
                    break
                feature_id = struct.unpack("Q", feature_id_data)[0]

                # 读取属性数量
                prop_count_data = f.read(4)
                if len(prop_count_data) < 4:
                    break
                prop_count = struct.unpack("I", prop_count_data)[0]

                # 跳过属性数据 (key_id + value_id pairs)
                data_size = prop_count * 8
                f.seek(data_size, 1)

                offsets[feature_id] = current_offset

        return offsets

    def load_index_from_file(self, index_file: str) -> None:
        """从索引文件加载索引"""
        if not os.path.exists(index_file):
            print(f"索引文件不存在: {index_file}")
            return

        try:
            with open(index_file, "r") as f:
                index_data = json.load(f)

            # 验证JSON结构
            if not index_data.get("version") or not index_data.get("data", {}).get(
                "features"
            ):
                print("索引文件格式不正确")
                return

            print(f"索引文件格式: JSON")
            print(f"版本: {index_data['version']}")
            print(f"要素数量: {len(index_data['data']['features'])}")

            # 构建偏移索引
            self._offset_index = {}
            count = 0
            for fid_str, feature_data in index_data["data"]["features"].items():
                fid = int(fid_str)
                attr_offset = feature_data.get("attr_offset", 0)
                self._offset_index[fid] = attr_offset

                if count < 5:  # 只显示前5个条目
                    print(f"索引条目 {count}: FID={fid}, attr_offset={attr_offset}")
                count += 1

            print(f"加载的索引条目数量: {len(self._offset_index)}")
            self._index_built = True

        except json.JSONDecodeError as e:
            print(f"JSON索引文件解析失败: {e}")

    def save_string_pool(self) -> None:
        """保存字符串池到文件"""
        pool_data = self.serializer.serialize_string_pool()

        with open(self.string_pool_file, "wb") as f:
            f.write(pool_data)
        print(f"字符串池已保存到: {self.string_pool_file}")

    def load_string_pool(self) -> None:
        """从文件加载字符串池"""
        if not os.path.exists(self.string_pool_file):
            print(f"字符串池文件不存在: {self.string_pool_file}")
            return

        with open(self.string_pool_file, "rb") as f:
            pool_data = f.read()

        self.serializer.deserialize_string_pool(pool_data)
        stats = self.serializer.get_compression_stats()
        print(f"字符串池已加载: {stats['unique_strings']} 个唯一字符串")

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        offsets = self._get_offset_index()
        return list(offsets.keys())

    def has_feature(self, feature_id: int) -> bool:
        """检查要素是否存在"""
        offsets = self._get_offset_index()
        return feature_id in offsets

    def clear_cache(self) -> None:
        """清除缓存"""
        self._offset_index = None
        self._index_built = False

    def get_attribute_file_path(self) -> str:
        """获取属性文件路径"""
        return self.attribute_file

    def get_string_pool_file_path(self) -> str:
        """获取字符串池文件路径"""
        return self.string_pool_file

    def get_compression_stats(self) -> dict:
        """获取压缩统计信息"""
        return self.serializer.get_compression_stats()

    def get_storage_stats(self) -> dict:
        """获取存储统计信息"""
        compression_stats = self.serializer.get_compression_stats()
        offsets = self._get_offset_index()

        return {
            "total_features": len(offsets),
            "total_original_size": compression_stats["original_size"],
            "total_compressed_size": compression_stats["compressed_size"],
            "compression_ratio": compression_stats["compression_ratio"],
            "string_pool_size": self.serializer.get_pool_size(),
            "string_pool_saved_bytes": compression_stats["original_size"]
            - compression_stats["compressed_size"],
        }
