# storage.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import os
import struct
import json
from typing import Dict, List, Tuple
from gisstorage.serializers import GeometrySerializer, AttributeSerializer
from gisstorage.models import GeometryData, AttributeData


class GeometryStorage:
    """几何数据存储类"""

    @staticmethod
    def calculate_geometry_size(geometry: GeometryData) -> int:
        """计算几何对象序列化后的大小"""
        # feature_id(8) + geometry_type(1) + bbox(32) + coord_size(4) + coords
        return 8 + 1 + 32 + 4 + len(geometry.coordinates)

    @staticmethod
    def get_serialized_size_from_header(data: bytes) -> int:
        """从数据头部获取完整序列化数据的大小"""
        if len(data) < 48:  # 最小头部长度
            return 0

        # 解析坐标数据长度
        coord_size = struct.unpack("I", data[44:48])[0]
        # 总大小 = 头部(48) + 坐标大小字段(4) + 坐标数据(coord_size)
        return 48 + 4 + coord_size

    def __init__(self, geometry_file: str):
        self.geometry_file = geometry_file
        self._offset_index = None  # 缓存偏移索引
        self._index_built = False  # 标记索引是否已构建
        # 创建目录
        os.makedirs(os.path.dirname(geometry_file), exist_ok=True)

    def write_geometry(self, geometry: GeometryData) -> int:
        with open(self.geometry_file, "ab") as f:
            offset = f.tell()
            geom_binary = GeometrySerializer.serialize_geometry(geometry)
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
            # 先读取头部数据以确定需要读取的总大小
            header_data = f.read(48)  # 读取头部信息
            if len(header_data) < 48:
                raise ValueError(f"几何数据不完整 for FID {feature_id}")

            # 获取坐标数据大小
            coord_size_data = f.read(4)  # 读取坐标大小字段
            if len(coord_size_data) < 4:
                raise ValueError(f"几何数据不完整 for FID {feature_id}")

            coord_size = struct.unpack("I", coord_size_data)[0]

            # 读取坐标数据
            coord_data = f.read(coord_size)
            if len(coord_data) < coord_size:
                raise ValueError(f"几何坐标数据不完整 for FID {feature_id}")

            # 组合所有数据进行反序列化
            geom_data = header_data + coord_size_data + coord_data
            geometry, _ = GeometrySerializer.deserialize_geometry(geom_data)
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
                current_pos = f.tell()

                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break  # 文件结束

                fid = struct.unpack("Q", fid_data)[0]
                offsets[fid] = current_pos

                # 手动解析记录结构
                try:
                    # 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                    f.seek(40, 1)

                    # 读取坐标大小(4B)
                    coord_size_data = f.read(4)
                    if len(coord_size_data) < 4:
                        break
                    coord_size = struct.unpack("I", coord_size_data)[0]

                    # 跳过坐标数据
                    f.seek(coord_size, 1)

                except Exception as e:
                    print(f"解析几何记录失败 at FID {fid}: {str(e)}")
                    break

        return offsets

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        # 使用缓存的偏移索引
        feature_offsets = self._get_offset_index()
        return list(feature_offsets.keys())

    def _is_valid_fid(self, fid: int) -> bool:
        # 使用缓存的偏移索引
        feature_offsets = self._get_offset_index()
        return fid in feature_offsets

    def clear_cache(self):
        """清除索引缓存"""
        self._offset_index = None
        self._index_built = False


class AttributeStorage:
    """属性数据存储类"""

    def __init__(self, attribute_file: str):
        self.attribute_file = attribute_file
        self._offset_index = None  # 缓存偏移索引
        self._index_built = False  # 标记索引是否已构建
        # 创建目录
        os.makedirs(os.path.dirname(attribute_file), exist_ok=True)

    def write_attribute(self, attribute: AttributeData) -> int:
        """写入属性数据"""
        with open(self.attribute_file, "ab") as f:
            offset = f.tell()
            attr_binary = AttributeSerializer.serialize_attributes(attribute)
            f.write(attr_binary)
            # 写入后清除索引缓存，因为文件已改变
            self._offset_index = None
            self._index_built = False
            return offset

    def read_attribute(self, feature_id: int) -> AttributeData:
        """根据要素ID读取属性数据"""
        if not os.path.exists(self.attribute_file):
            return None

        # 使用缓存的偏移索引
        feature_offsets = self._get_offset_index()
        if feature_id not in feature_offsets:
            return None

        offset = feature_offsets[feature_id]
        with open(self.attribute_file, "rb") as f:
            f.seek(offset)
            # 读取feature_id和json长度
            header_data = f.read(12)
            if len(header_data) < 12:
                return None

            fid, json_length = struct.unpack("QI", header_data)
            if fid == feature_id:
                # 读取属性数据
                props_data = f.read(json_length)
                props_json = props_data.decode("utf-8")
                properties = json.loads(props_json)
                return AttributeData(feature_id, properties)

        return None

    def _get_offset_index(self) -> Dict[int, int]:
        """获取偏移索引，使用缓存机制"""
        if self._offset_index is None or not self._index_built:
            self._offset_index = self._build_offset_index()
            self._index_built = True
        return self._offset_index

    def _build_offset_index(self) -> Dict[int, int]:
        """构建属性数据的偏移索引"""
        offsets = {}
        if not os.path.exists(self.attribute_file):
            return offsets

        with open(self.attribute_file, "rb") as f:
            # 跳过文件开头的字段信息
            try:
                # 读取字段信息长度
                field_info_length_data = f.read(4)
                if len(field_info_length_data) >= 4:
                    field_info_length = struct.unpack("I", field_info_length_data)[0]
                    # 跳过字段信息
                    f.seek(field_info_length, 1)
            except:
                # 如果读取字段信息失败，重置文件指针到开头
                f.seek(0)

            while True:
                current_pos = f.tell()

                # 读取feature_id和json长度
                header_data = f.read(12)
                if len(header_data) < 12:
                    break

                fid, json_length = struct.unpack("QI", header_data)
                offsets[fid] = current_pos

                # 跳过属性数据
                f.seek(json_length, 1)

        return offsets

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        # 使用缓存的偏移索引
        feature_offsets = self._get_offset_index()
        return list(feature_offsets.keys())

    def clear_cache(self):
        """清除索引缓存"""
        self._offset_index = None
        self._index_built = False
