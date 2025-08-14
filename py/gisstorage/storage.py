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
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break  # 文件结束

                fid = struct.unpack('Q', fid_data)[0]
                offsets[fid] = current_pos

                # 正确解析整个记录结构（不包含S2字段）
                try:
                    # geometry_type(1B)
                    f.seek(1, 1)

                    # bbox(32B)
                    f.seek(32, 1)

                    # coord_size(4B)
                    coord_size = struct.unpack('I', f.read(4))[0]

                    # coordinates(coord_size)
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
                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break

                fid = struct.unpack('Q', fid_data)[0]
                feature_ids.append(fid)

                # 跳到下一个记录
                # 读取geometry_type
                geom_type_data = f.read(1)
                if len(geom_type_data) < 1:
                    break

                # 读取bbox
                f.read(32)

                # 读取坐标大小并跳过坐标数据
                coord_size_data = f.read(4)
                if len(coord_size_data) < 4:
                    break
                coord_size = struct.unpack('I', coord_size_data)[0]
                f.read(coord_size)

        return feature_ids

    def _is_valid_fid(self, fid: int) -> bool:
        # 直接检查文件是否存在该FID
        if not os.path.exists(self.geometry_file):
            return False

        with open(self.geometry_file, 'rb') as f:
            while True:
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    return False

                current_fid = struct.unpack('Q', fid_data)[0]
                if current_fid == fid:
                    return True

                # 跳过当前记录的其余部分
                try:
                    f.seek(1, 1)  # geometry_type
                    f.seek(32, 1)  # bbox
                    coord_size = struct.unpack('I', f.read(4))[0]
                    f.seek(coord_size, 1)
                except:
                    return False


class AttributeStorage:
    """属性数据存储类"""

    def __init__(self, attribute_file: str):
        self.attribute_file = attribute_file
        # 创建目录
        os.makedirs(os.path.dirname(attribute_file), exist_ok=True)

    def write_attribute(self, attribute: AttributeData):
        """写入属性数据"""
        with open(self.attribute_file, 'ab') as f:
            attr_binary = AttributeSerializer.serialize_attributes(attribute)
            f.write(attr_binary)

    def read_attribute(self, feature_id: int) -> Dict:
        """根据要素ID读取属性数据"""
        if not os.path.exists(self.attribute_file):
            return {}

        with open(self.attribute_file, 'rb') as attr_file:
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
                    return json.loads(props_json)
                else:
                    # 跳过属性数据
                    attr_file.seek(json_length, 1)

        return {}
