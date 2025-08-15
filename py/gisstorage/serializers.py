# serializers.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import struct
import json
from typing import List, Tuple
from gisstorage.models import GeometryData, AttributeData


class GeometrySerializer:
    """几何数据序列化器"""

    @staticmethod
    def serialize_coordinates(coords: List[Tuple[float, float]]) -> bytes:
        """序列化坐标数据为二进制格式"""
        # 先写入点的数量
        data = struct.pack('I', len(coords))
        # 再写入所有坐标点
        for x, y in coords:
            data += struct.pack('dd', x, y)
        return data

    @staticmethod
    def deserialize_coordinates(
            data: bytes) -> Tuple[List[Tuple[float, float]], int]:
        """从二进制数据反序列化坐标"""
        num_points = struct.unpack('I', data[:4])[0]
        coords = []
        offset = 4
        for _ in range(num_points):
            x, y = struct.unpack('dd', data[offset:offset + 16])
            coords.append((x, y))
            offset += 16
        return coords, offset

    @staticmethod
    def serialize_geometry(geometry: GeometryData) -> bytes:
        """序列化整个几何对象"""
        # 格式: feature_id(8) + geometry_type(1) + bbox(32) + coord_size(4) + coords
        data = struct.pack('Qbdddd', geometry.feature_id,
                           geometry.geometry_type, geometry.bbox[0],
                           geometry.bbox[1], geometry.bbox[2],
                           geometry.bbox[3])

        # 写入坐标数据
        coord_size = len(geometry.coordinates)
        data += struct.pack('I', coord_size)
        data += geometry.coordinates
        return data

    @staticmethod
    def deserialize_geometry(data: bytes) -> Tuple[GeometryData, int]:
        """从二进制数据反序列化几何对象"""
        if len(data) < 48:  # 最小长度：8+1+8+8+8+8+8=41字节，但实际需要48字节（内存对齐）
            raise ValueError("数据长度不足，无法反序列化几何对象")

        # 解析基本字段
        feature_id, geometry_type, min_x, min_y, max_x, max_y = \
            struct.unpack('Qbdddd', data[:48])

        offset = 48

        # 检查坐标数据长度
        if offset + 4 > len(data):
            raise ValueError("坐标数据长度不足")

        coord_size = struct.unpack('I', data[offset:offset + 4])[0]
        offset += 4

        if offset + coord_size > len(data):
            raise ValueError("坐标数据不完整")

        # 获取压缩的坐标数据
        compressed_coords = data[offset:offset + coord_size]

        return GeometryData(feature_id, geometry_type, compressed_coords,
                            (min_x, min_y, max_x, max_y)), offset + coord_size


class AttributeSerializer:
    """属性数据序列化器"""

    @staticmethod
    def serialize_attributes(attr: AttributeData) -> bytes:
        """序列化属性数据"""
        # 将属性转换为JSON字符串再编码为bytes
        props_json = json.dumps(attr.properties, ensure_ascii=False)
        props_bytes = props_json.encode('utf-8')

        # 格式: feature_id(8) + json_length(4) + json_data
        data = struct.pack('QI', attr.feature_id, len(props_bytes))
        data += props_bytes
        return data

    @staticmethod
    def deserialize_attributes(data: bytes) -> Tuple[AttributeData, int]:
        """反序列化属性数据"""
        feature_id, json_length = struct.unpack('QI', data[:12])
        props_json = data[12:12 + json_length].decode('utf-8')
        properties = json.loads(props_json)

        attr = AttributeData(feature_id, properties)
        return attr, 12 + json_length
