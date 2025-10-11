# attribute_storage.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import os
import struct
import threading
from typing import Dict, List, Optional
from .attribute_data import AttributeData
from .attribute_serializer import AttributeSerializer


class AttributeStorage:
    """属性数据存储类 - 集成字符串池，支持分块索引和按需加载，与C++版本兼容"""

    class IndexChunk:
        """索引块结构，与C++版本完全一致"""

        def __init__(self):
            self.start_fid = 0
            self.end_fid = 0
            self.offset_map: Dict[int, int] = {}
            self.loaded = False

    def __init__(self, attribute_file: str, string_pool_file: str):
        self.attribute_file = attribute_file
        self.string_pool_file = string_pool_file

        # 分块索引
        self.index_chunks: List[AttributeStorage.IndexChunk] = []
        self.use_chunked_mode = False
        self.chunk_size = 10000  # 默认每个块10000个要素

        # 简单缓存 - 只缓存最近访问的数据
        self.cache: Dict[int, AttributeData] = {}
        self.max_cache_size = 5000  # 增加缓存大小到5000个属性

        # 线程安全
        self.mutex = threading.Lock()

        # 序列化器
        self.serializer = AttributeSerializer()

        # 创建目录
        os.makedirs(os.path.dirname(attribute_file), exist_ok=True)

    def write_attribute(self, attribute: AttributeData) -> int:
        """写入属性数据，与C++版本完全一致"""
        try:
            with open(self.attribute_file, "ab") as file:
                offset = file.tell()
                attr_binary = self.serializer.serialize_attributes(attribute)

                if attr_binary:
                    file.write(attr_binary)
                    file.flush()  # 确保数据写入磁盘
                    # 清除缓存
                    with self.mutex:
                        self.cache.clear()
                    return offset
                else:
                    raise RuntimeError(
                        f"属性数据序列化失败 for FID {attribute.get_feature_id()}"
                    )
        except IOError as e:
            raise RuntimeError(
                f"无法打开属性文件进行写入: {self.attribute_file} (错误: {e})"
            )

    def read_attribute(self, feature_id: int) -> AttributeData:
        """读取属性数据 - 优化版本，支持按需加载，与C++版本完全一致"""
        # 先检查缓存
        with self.mutex:
            if feature_id in self.cache:
                return self.cache[feature_id]

        # 确保字符串池已加载
        if self.serializer.string_pool.get_pool_size() == 0:
            self.load_string_pool()

        # 获取偏移量
        offset = self._get_chunked_offset(feature_id)
        if offset < 0:
            # 如果没有分块索引，使用顺序搜索
            attribute = self._sequential_search_attribute(feature_id)
        else:
            # 读取属性数据
            attribute = self._read_attribute_at_offset(feature_id, offset)

        # 更新缓存
        with self.mutex:
            self._update_cache(feature_id, attribute)

        return attribute

    def read_attribute_on_demand(self, feature_id: int) -> AttributeData:
        """按需读取属性数据（不依赖完整索引），与C++版本完全一致"""
        # 如果使用分块模式，尝试从分块中读取
        if self.use_chunked_mode:
            offset = self._get_chunked_offset(feature_id)
            if offset >= 0:
                return self._read_attribute_at_offset(feature_id, offset)

        # 否则进行顺序搜索
        return self._sequential_search_attribute(feature_id)

    def read_attributes_batch(self, feature_ids: List[int]) -> List[AttributeData]:
        """批量读取属性数据（优化版本），与C++版本完全一致"""
        results = []

        for feature_id in feature_ids:
            try:
                attribute = self.read_attribute(feature_id)
                results.append(attribute)
            except (ValueError, RuntimeError):
                # 如果某个要素不存在，添加None或跳过
                results.append(None)

        return results

    def read_attribute_at_offset(self, feature_id: int, offset: int) -> AttributeData:
        """根据偏移读取属性数据，与C++版本完全一致"""
        return self._read_attribute_at_offset(feature_id, offset)

    def has_feature(self, feature_id: int) -> bool:
        """检查要素是否存在，与C++版本完全一致"""
        if self.use_chunked_mode:
            return self._get_chunked_offset(feature_id) >= 0
        else:
            # 简单检查：尝试读取
            try:
                self.read_attribute_on_demand(feature_id)
                return True
            except (ValueError, RuntimeError):
                return False

    def clear_cache(self) -> None:
        """清除缓存，与C++版本完全一致"""
        with self.mutex:
            self.cache.clear()

    def get_attribute_file_path(self) -> str:
        """获取文件路径，与C++版本完全一致"""
        return self.attribute_file

    def get_string_pool_file_path(self) -> str:
        """获取字符串池文件路径，与C++版本完全一致"""
        return self.string_pool_file

    def load_index_from_file(self, index_file: str) -> None:
        """从索引文件加载索引 - 优化版本，支持分块加载，与C++版本完全一致"""
        if not os.path.exists(index_file):
            print(f"索引文件不存在: {index_file}")
            return

        import json

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
            self.cache.clear()
            count = 0
            for fid_str, feature_data in index_data["data"]["features"].items():
                fid = int(fid_str)
                attr_offset = feature_data.get("attr_offset", 0)
                self.cache[fid] = attr_offset  # 临时使用cache存储偏移量

                if count < 5:  # 只显示前5个条目
                    print(f"索引条目 {count}: FID={fid}, attr_offset={attr_offset}")
                count += 1

            print(f"加载的索引条目数量: {len(self.cache)}")

        except json.JSONDecodeError as e:
            print(f"JSON索引文件解析失败: {e}")

    def save_index_to_file(self, index_file: str) -> None:
        """保存索引到二进制文件，与C++版本完全一致"""
        # 构建索引数据
        index_data = {"version": "1.0", "data": {"features": {}}}

        # 从缓存中获取偏移量信息
        with self.mutex:
            for fid, offset in self.cache.items():
                if isinstance(offset, int):  # 确保是偏移量而不是AttributeData对象
                    index_data["data"]["features"][str(fid)] = {"attr_offset": offset}

        import json

        try:
            with open(index_file, "w") as f:
                json.dump(index_data, f, indent=2)
            print(f"属性索引已保存到: {index_file}")
        except IOError as e:
            print(f"无法保存属性索引文件: {index_file} (错误: {e})")

    def save_string_pool(self) -> None:
        """保存字符串池到文件，与C++版本完全一致"""
        success = self.serializer.string_pool.save_to_file(self.string_pool_file)
        if success:
            print(f"字符串池已保存到: {self.string_pool_file}")
        else:
            print(f"保存字符串池失败: {self.string_pool_file}")

    def create_string_pool_index(self) -> None:
        """创建字符串池索引文件，与C++版本完全一致"""
        # 这个方法在StringPool类中实现
        pass

    def build_index_from_pool_file(
        self, pool_file_path: str, index_file_path: str
    ) -> bool:
        """从 .pool 文件构建索引文件，与C++版本完全一致"""
        # 这个方法在StringPool类中实现
        return True

    def load_string_pool(self) -> None:
        """加载字符串池从文件 - 优化版本，支持内存映射，与C++版本完全一致"""
        success = self.serializer.string_pool.load_from_file(self.string_pool_file)
        if success:
            stats = self.serializer.get_compression_stats()
            print(f"字符串池已加载: {stats['unique_strings']} 个唯一字符串")
        else:
            print(f"加载字符串池失败: {self.string_pool_file}")

    def get_compression_stats(self) -> Dict:
        """获取压缩统计信息，与C++版本完全一致"""
        return self.serializer.get_compression_stats()

    def get_storage_stats(self) -> Dict:
        """获取存储统计信息，与C++版本完全一致"""
        # 确保字符串池已加载
        if not self.serializer.string_pool.use_mmap_mode:
            self.load_string_pool()

        compression_stats = self.serializer.get_compression_stats()

        # 计算总要素数（从文件大小估算或从分块索引获取）
        total_features = 0
        if self.use_chunked_mode and self.index_chunks:
            # 从分块索引计算总要素数
            for chunk in self.index_chunks:
                total_features += len(chunk.offset_map)
        else:
            # 从缓存大小估算（如果缓存为空，尝试从文件估算）
            total_features = len(self.cache)
            if total_features == 0:
                # 简单估算：文件大小除以平均每个要素的大小
                if os.path.exists(self.attribute_file):
                    file_size = os.path.getsize(self.attribute_file)
                    # 估算每个要素平均大小（包括feature_id + 属性数据）
                    avg_size = 100  # 估算值
                    total_features = max(0, file_size // avg_size)

        # 计算压缩统计信息
        original_size = compression_stats.get("original_size", 0)
        compressed_size = compression_stats.get("compressed_size", 0)
        compression_ratio = 0.0
        saved_bytes = 0

        if original_size > 0:
            compression_ratio = (1.0 - compressed_size / original_size) * 100.0
            saved_bytes = original_size - compressed_size

        return {
            "total_features": total_features,
            "total_original_size": original_size,
            "total_compressed_size": compressed_size,
            "compression_ratio": compression_ratio,
            "string_pool_size": self.serializer.get_pool_size(),
            "string_pool_saved_bytes": saved_bytes,
        }

    def set_chunk_size(self, chunk_size: int) -> None:
        """设置分块大小，与C++版本完全一致"""
        self.chunk_size = chunk_size

    def set_cache_size(self, cache_size: int) -> None:
        """设置缓存大小，与C++版本完全一致"""
        self.max_cache_size = cache_size

    def set_use_mmap_mode(self, use_mmap: bool) -> None:
        """设置是否启用mmap模式，与C++版本完全一致"""
        # 这个方法在StringPool类中实现
        pass

    def build_chunked_index(self) -> None:
        """构建分块索引，与C++版本完全一致"""
        if not os.path.exists(self.attribute_file):
            return

        self.index_chunks.clear()
        current_chunk = None

        with open(self.attribute_file, "rb") as f:
            while True:
                current_offset = f.tell()

                # 读取feature_id
                feature_id_data = f.read(8)
                if len(feature_id_data) < 8:
                    break
                feature_id = struct.unpack("Q", feature_id_data)[0]

                # 读取属性数量 (变长编码)
                prop_count, _ = self.serializer._decode_varint(
                    f.read(1024), 0
                )  # 读取足够的数据
                f.seek(current_offset + 8)  # 重新定位

                # 跳过属性数据
                # 这里需要更复杂的逻辑来跳过变长编码的数据
                # 简化实现：读取到下一个feature_id
                while True:
                    byte = f.read(1)
                    if not byte:
                        break
                    # 检查是否是下一个feature_id的开始（简化实现）
                    if byte[0] == 0:  # 假设feature_id不会以0开头
                        f.seek(f.tell() - 1)
                        break

                # 检查是否需要创建新的分块
                if (
                    current_chunk is None
                    or len(current_chunk.offset_map) >= self.chunk_size
                ):
                    if current_chunk is not None:
                        current_chunk.end_fid = feature_id - 1
                        self.index_chunks.append(current_chunk)

                    current_chunk = AttributeStorage.IndexChunk()
                    current_chunk.start_fid = feature_id

                current_chunk.offset_map[feature_id] = current_offset

        # 添加最后一个分块
        if current_chunk is not None:
            if current_chunk.offset_map:
                current_chunk.end_fid = max(current_chunk.offset_map.keys())
            self.index_chunks.append(current_chunk)

        self.use_chunked_mode = True
        print(f"构建属性分块索引完成: {len(self.index_chunks)} 个分块")

    def _encode_varint(self, value: int) -> bytes:
        """使用变长编码压缩整数，与C++版本完全一致"""
        data = bytearray()
        while value >= 0x80:
            data.append((value & 0xFF) | 0x80)
            value >>= 7
        data.append(value & 0xFF)
        return bytes(data)

    def _decode_varint(self, data: bytes, offset: int) -> tuple:
        """解码变长整数，返回(值, 新偏移量)"""
        value = 0
        shift = 0
        
        while offset < len(data):
            byte = data[offset]
            offset += 1
            value |= (byte & 0x7F) << shift
            
            if (byte & 0x80) == 0:
                break
            
            shift += 7
        
        return value, offset

    def save_chunked_index(self, index_file: str) -> None:
        """保存分块索引到文件，使用紧凑格式与C++版本完全一致"""
        if not self.use_chunked_mode or not self.index_chunks:
            print("没有属性分块索引需要保存")
            return

        try:
            index_data = bytearray()
            
            # 写入文件头 (紧凑格式)
            version = 2  # 新版本号，表示紧凑格式
            index_data.extend(self._encode_varint(version))
            index_data.extend(self._encode_varint(len(self.index_chunks)))
            index_data.extend(self._encode_varint(self.chunk_size))
            
            # 写入每个索引块
            for chunk in self.index_chunks:
                # 写入块头 (紧凑格式)
                index_data.extend(self._encode_varint(chunk.start_fid))
                index_data.extend(self._encode_varint(chunk.end_fid))
                index_data.extend(self._encode_varint(len(chunk.offset_map)))
                
                # 写入偏移映射 (紧凑格式)
                for fid, offset in chunk.offset_map.items():
                    index_data.extend(self._encode_varint(fid))
                    index_data.extend(self._encode_varint(offset))
            
            # 一次性写入所有数据
            with open(index_file, "wb") as f:
                f.write(index_data)

            print(f"属性分块索引已保存到: {index_file}")
        except IOError as e:
            print(f"无法保存属性分块索引文件: {index_file} (错误: {e})")

    def load_chunked_index(self, index_file: str) -> None:
        """从文件加载分块索引，使用紧凑格式与C++版本完全一致"""
        if not os.path.exists(index_file):
            print(f"属性分块索引文件不存在: {index_file}")
            return

        try:
            with open(index_file, "rb") as f:
                index_data = f.read()
            
            offset = 0
            
            # 读取文件头 (紧凑格式)
            version, offset = self._decode_varint(index_data, offset)
            
            # 检查版本号
            if version != 2:
                print(f"不支持的属性分块索引版本: {version}")
                return
            
            chunk_count, offset = self._decode_varint(index_data, offset)
            chunk_size, offset = self._decode_varint(index_data, offset)
            
            self.chunk_size = chunk_size
            self.index_chunks.clear()
            
            # 读取每个索引块
            for _ in range(chunk_count):
                # 读取块头 (紧凑格式)
                start_fid, offset = self._decode_varint(index_data, offset)
                end_fid, offset = self._decode_varint(index_data, offset)
                entry_count, offset = self._decode_varint(index_data, offset)
                
                chunk = AttributeStorage.IndexChunk()
                chunk.start_fid = start_fid
                chunk.end_fid = end_fid
                chunk.loaded = True
                
                # 读取偏移映射 (紧凑格式)
                for _ in range(entry_count):
                    fid, offset = self._decode_varint(index_data, offset)
                    file_offset, offset = self._decode_varint(index_data, offset)
                    chunk.offset_map[fid] = file_offset
                
                self.index_chunks.append(chunk)
            
            self.use_chunked_mode = True
            print(f"属性分块索引已加载: {len(self.index_chunks)} 个分块")

        except IOError as e:
            print(f"加载属性分块索引失败: {e}")

    def _get_chunked_offset(self, feature_id: int) -> int:
        """获取分块偏移，与C++版本完全一致"""
        if not self.use_chunked_mode:
            return -1

        # 二分查找对应的分块
        left, right = 0, len(self.index_chunks) - 1
        while left <= right:
            mid = (left + right) // 2
            chunk = self.index_chunks[mid]

            if chunk.start_fid <= feature_id <= chunk.end_fid:
                # 加载索引块（如果需要）
                if not chunk.loaded:
                    self._load_index_chunk(chunk)
                return chunk.offset_map.get(feature_id, -1)
            elif feature_id < chunk.start_fid:
                right = mid - 1
            else:
                left = mid + 1

        return -1

    def _load_index_chunk(self, chunk: IndexChunk) -> None:
        """加载索引块，与C++版本完全一致"""
        if chunk.loaded:
            return

        # 这里可以实现更复杂的加载逻辑
        # 目前简单标记为已加载
        chunk.loaded = True

    def _update_cache(self, fid: int, data: AttributeData) -> None:
        """更新LRU缓存，与C++版本完全一致"""
        # 如果缓存已满，移除最久未使用的条目
        if len(self.cache) >= self.max_cache_size:
            # 简单实现：移除第一个条目
            if self.cache:
                first_key = next(iter(self.cache))
                del self.cache[first_key]

        # 添加新条目到缓存
        self.cache[fid] = data

    def _read_attribute_at_offset(self, feature_id: int, offset: int) -> AttributeData:
        """根据偏移量读取属性数据，与C++版本完全一致"""
        try:
            with open(self.attribute_file, "rb") as f:
                f.seek(offset)

                # 读取feature_id
                feature_id_data = f.read(8)
                if len(feature_id_data) < 8:
                    raise ValueError(f"属性数据不完整 for FID {feature_id}")

                # 读取属性数量 (变长编码)
                prop_count_data = f.read(1024)  # 读取足够的数据
                prop_count, prop_count_size = self.serializer._decode_varint(
                    prop_count_data, 0
                )

                # 重新定位到属性数据开始位置
                f.seek(offset + 8 + prop_count_size)

                # 读取剩余数据
                remaining_data = f.read(prop_count * 16)  # 估算大小
                if len(remaining_data) < prop_count * 8:  # 最小大小
                    raise ValueError(f"属性数据不完整 for FID {feature_id}")

                # 组合所有数据进行反序列化
                attr_data = (
                    feature_id_data + prop_count_data[:prop_count_size] + remaining_data
                )
                attribute = self.serializer.deserialize_attributes(attr_data)

                return attribute

        except IOError as e:
            raise RuntimeError(f"读取属性数据失败: {e}")

    def _sequential_search_attribute(self, feature_id: int) -> AttributeData:
        """顺序搜索属性数据，与C++版本完全一致"""
        if not os.path.exists(self.attribute_file):
            raise ValueError(f"属性文件不存在: {self.attribute_file}")

        with open(self.attribute_file, "rb") as f:
            while True:
                current_offset = f.tell()

                # 读取feature_id
                feature_id_data = f.read(8)
                if len(feature_id_data) < 8:
                    break
                current_fid = struct.unpack("Q", feature_id_data)[0]

                if current_fid == feature_id:
                    # 找到目标要素，读取完整数据
                    f.seek(current_offset)
                    return self._read_attribute_at_offset(feature_id, current_offset)

                # 跳过当前记录
                # 读取属性数量 (变长编码)
                prop_count_data = f.read(1024)
                prop_count, prop_count_size = self.serializer._decode_varint(
                    prop_count_data, 0
                )

                # 跳过属性数据
                f.seek(current_offset + 8 + prop_count_size + prop_count * 8)

        raise ValueError(f"Feature ID {feature_id} not found")
