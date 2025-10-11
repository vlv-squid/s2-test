# attribute_serializer.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import struct
from typing import Dict, Tuple
from .attribute_data import AttributeData
from .string_pool import StringPool


class AttributeSerializer:
    """属性序列化器类 - 集成字符串池，与C++版本兼容"""

    def __init__(self):
        self.string_pool = StringPool()
        self.stats = {
            "original_size": 0,
            "compressed_size": 0,
            "compression_ratio": 0.0,
            "unique_strings": 0,
            "total_strings": 0,
        }

    def serialize_attributes(self, attribute: AttributeData) -> bytes:
        """序列化属性数据（使用字符串池），与C++版本完全一致"""
        # 获取属性
        properties = attribute.get_properties()

        # 紧凑格式：使用变长编码优化存储
        data = bytearray()

        # 写入feature_id (8字节)
        feature_id = attribute.get_feature_id()
        data.extend(struct.pack("Q", feature_id))

        # 写入属性数量 (变长编码，通常1-2字节)
        prop_count = len(properties)
        self._encode_varint(data, prop_count)

        # 计算原始大小（用于统计）
        original_size = 8 + 4  # feature_id + prop_count

        # 序列化每个属性对 - 使用紧凑格式
        for key, value in properties.items():
            # 获取字符串ID（如果不存在则添加到池中）
            key_id = self.string_pool.get_string_id(key)
            value_id = self.string_pool.get_string_id(value)

            # 使用变长编码存储ID，通常可以节省1-2字节
            self._encode_varint(data, key_id)
            self._encode_varint(data, value_id)

            # 计算原始大小
            original_size += len(key) + len(value) + 2  # 字符串长度 + 引号

        # 更新统计信息
        self._update_stats(original_size, len(data))

        return bytes(data)

    def deserialize_attributes(self, data: bytes) -> AttributeData:
        """反序列化属性数据（从字符串池恢复），与C++版本完全一致"""
        if len(data) < 9:  # 至少需要feature_id + 1字节的属性数量
            raise ValueError("属性数据长度不足")

        offset = 0

        # 读取feature_id
        feature_id = struct.unpack("Q", data[offset : offset + 8])[0]
        offset += 8

        # 读取属性数量 (变长编码)
        prop_count, offset = self._decode_varint(data, offset)

        # 创建属性映射
        properties = {}

        # 重建属性映射
        for i in range(prop_count):
            if offset >= len(data):
                raise ValueError("属性数据不完整")

            # 读取key_id和value_id (变长编码)
            key_id, offset = self._decode_varint(data, offset)
            if offset >= len(data):
                raise ValueError("属性数据不完整")
            value_id, offset = self._decode_varint(data, offset)

            # 从字符串池中获取字符串
            key = self.string_pool.get_string(key_id)
            value = self.string_pool.get_string(value_id)

            # 检查字符串ID是否超出字符串池范围
            if (
                key_id >= self.string_pool.get_pool_size()
                or value_id >= self.string_pool.get_pool_size()
            ):
                raise ValueError(
                    f"字符串池中找不到对应的字符串 - key_id: {key_id}, value_id: {value_id}, 字符串池大小: {self.string_pool.get_pool_size()}"
                )

            # 允许空字符串，因为某些属性字段可能确实为空
            properties[key] = value

        return AttributeData(feature_id, properties)

    def serialize_string_pool(self) -> bytes:
        """序列化字符串池"""
        return self.string_pool.serialize()

    def deserialize_string_pool(self, data: bytes) -> None:
        """反序列化字符串池"""
        self.string_pool.deserialize(data)

    def get_compression_stats(self) -> Dict:
        """获取压缩率统计"""
        self.stats["unique_strings"] = self.string_pool.get_pool_size()
        self.stats["total_strings"] = self.string_pool.get_total_size()
        return self.stats.copy()

    def get_pool_size(self) -> int:
        """获取字符串池大小"""
        return self.string_pool.get_pool_size()

    def get_pool_total_size(self) -> int:
        """获取字符串池总大小"""
        return self.string_pool.get_total_size()

    def clear_string_pool(self) -> None:
        """清空字符串池"""
        self.string_pool.clear()

    def load_string_pool_from_file(self, file_path: str) -> bool:
        """从文件加载字符串池（使用mmap）"""
        return self.string_pool.load_from_file(file_path)

    def get_string_pool(self) -> StringPool:
        """获取字符串池引用（用于直接访问mmap功能）"""
        return self.string_pool

    def _update_stats(self, original_size: int, compressed_size: int) -> None:
        """更新统计信息"""
        self.stats["original_size"] += original_size
        self.stats["compressed_size"] += compressed_size

        if self.stats["original_size"] > 0:
            self.stats["compression_ratio"] = (
                1.0 - self.stats["compressed_size"] / self.stats["original_size"]
            ) * 100.0

    def _encode_varint(self, data: bytearray, value: int) -> None:
        """变长编码辅助函数（公开接口）"""
        # 使用变长编码，每个字节的最高位表示是否还有后续字节
        while value >= 0x80:
            data.append((value & 0xFF) | 0x80)
            value >>= 7
        data.append(value & 0xFF)

    def _decode_varint(self, data: bytes, offset: int) -> Tuple[int, int]:
        """变长编码辅助函数（公开接口）"""
        value = 0
        shift = 0

        while offset < len(data):
            byte = data[offset]
            offset += 1
            value |= (byte & 0x7F) << shift

            if (byte & 0x80) == 0:
                break  # 最后一个字节

            shift += 7
            if shift >= 32:
                raise ValueError("变长编码值过大")

        return value, offset
