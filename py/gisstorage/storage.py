# storage.py
"""数据存储管理"""

import os
import struct
import json
from typing import Dict, List, Tuple
from gisstorage.serializers import GeometrySerializer, AttributeSerializer
from gisstorage.models import GeometryData, AttributeData


class GeometryStorage:
    """几何数据存储类"""

    def __init__(self, geometry_file: str):
        self.geometry_file = geometry_file
        # 创建目录
        os.makedirs(os.path.dirname(geometry_file), exist_ok=True)

    def write_geometry(self, geometry: GeometryData) -> int:
        with open(self.geometry_file, 'ab') as f:
            offset = f.tell()
            geom_binary = GeometrySerializer.serialize_geometry(geometry)
            # 写入前校验数据完整性
            if len(geom_binary) > 0:
                f.write(geom_binary)
                return offset
            else:
                raise ValueError(f"几何数据序列化失败 for FID {geometry.feature_id}")

    def read_geometry(self, feature_id: int) -> GeometryData:
        """根据要素ID读取几何数据"""
        feature_offsets = self._build_offset_index()
        if feature_id not in feature_offsets:
            raise ValueError(f"Feature ID {feature_id} not found")

        offset = feature_offsets[feature_id]
        with open(self.geometry_file, 'rb') as f:
            f.seek(offset)
            # 读取足够大的数据块（假设最大1MB）
            geom_data = f.read(1024 * 1024)
            geometry, _ = GeometrySerializer.deserialize_geometry(geom_data)
            return geometry

    def _build_offset_index(self) -> Dict[int, int]:
        offsets = {}
        if not os.path.exists(self.geometry_file):
            return offsets

        with open(self.geometry_file, 'rb') as f:
            while True:
                current_pos = f.tell()
                
                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break  # 文件结束

                fid = struct.unpack('Q', fid_data)[0]
                offsets[fid] = current_pos

                # 手动解析记录结构
                try:
                    # 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                    f.seek(40, 1)
                    
                    # 读取坐标大小(4B)
                    coord_size_data = f.read(4)
                    if len(coord_size_data) < 4:
                        break
                    coord_size = struct.unpack('I', coord_size_data)[0]
                    
                    # 跳过坐标数据
                    f.seek(coord_size, 1)

                except Exception as e:
                    print(f"解析几何记录失败 at FID {fid}: {str(e)}")
                    break

        return offsets

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID"""
        feature_ids = []
        if not os.path.exists(self.geometry_file):
            return feature_ids

        with open(self.geometry_file, 'rb') as f:
            while True:
                current_pos = f.tell()
                
                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break

                fid = struct.unpack('Q', fid_data)[0]
                feature_ids.append(fid)

                # 手动解析记录结构
                try:
                    # 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                    f.seek(40, 1)
                    
                    # 读取坐标大小(4B)
                    coord_size_data = f.read(4)
                    if len(coord_size_data) < 4:
                        break
                    coord_size = struct.unpack('I', coord_size_data)[0]
                    
                    # 跳过坐标数据
                    f.seek(coord_size, 1)

                except Exception as e:
                    print(f"解析几何记录失败 at FID {fid}: {str(e)}")
                    break

        return feature_ids

    def _is_valid_fid(self, fid: int) -> bool:
        # 直接检查文件是否存在该FID
        if not os.path.exists(self.geometry_file):
            return False

        with open(self.geometry_file, 'rb') as f:
            while True:
                current_pos = f.tell()
                
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    return False

                current_fid = struct.unpack('Q', fid_data)[0]
                if current_fid == fid:
                    return True

                # 手动解析记录结构
                try:
                    # 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                    f.seek(40, 1)
                    
                    # 读取坐标大小(4B)
                    coord_size_data = f.read(4)
                    if len(coord_size_data) < 4:
                        return False
                    coord_size = struct.unpack('I', coord_size_data)[0]
                    
                    # 跳过坐标数据
                    f.seek(coord_size, 1)

                except Exception as e:
                    return False


class AttributeStorage:
    """属性数据存储类"""

    def __init__(self, attribute_file: str):
        self.attribute_file = attribute_file
        # 创建目录
        os.makedirs(os.path.dirname(attribute_file), exist_ok=True)

    def write_attribute(self, attribute: AttributeData) -> int:
        """写入属性数据"""
        with open(self.attribute_file, 'ab') as f:
            offset = f.tell()
            attr_binary = AttributeSerializer.serialize_attributes(attribute)
            f.write(attr_binary)
            return offset

    def read_attribute(self, feature_id: int) -> AttributeData:
        """根据要素ID读取属性数据"""
        if not os.path.exists(self.attribute_file):
            return None

        with open(self.attribute_file, 'rb') as attr_file:
            # 跳过文件开头的字段信息
            try:
                # 读取字段信息长度
                field_info_length_data = attr_file.read(4)
                if len(field_info_length_data) >= 4:
                    field_info_length = struct.unpack('I', field_info_length_data)[0]
                    # 跳过字段信息
                    attr_file.seek(field_info_length, 1)
            except:
                # 如果读取字段信息失败，重置文件指针到开头
                attr_file.seek(0)

            while True:
                # 读取feature_id和json长度
                header_data = attr_file.read(12)
                if len(header_data) < 12:
                    break

                fid, json_length = struct.unpack('QI', header_data)
                if fid == feature_id:
                    # 读取属性数据
                    props_data = attr_file.read(json_length)
                    props_json = props_data.decode('utf-8')
                    properties = json.loads(props_json)
                    return AttributeData(feature_id, properties)
                else:
                    # 跳过属性数据
                    attr_file.seek(json_length, 1)

        return None
