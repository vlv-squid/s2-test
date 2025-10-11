# string_pool.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

import os
import mmap
import struct
import threading
from typing import Dict, List, Tuple, Optional
from collections import OrderedDict


class StringPool:
    """字符串池类 - 用于减少重复字符串的存储，与C++版本兼容"""

    def __init__(self):
        self.string_to_id: Dict[str, int] = {}
        self.string_table: List[str] = []
        self.string_offsets: List[int] = []
        self.total_size = 0
        self.string_count = 0
        self.use_mmap_mode = False

        # 内存映射相关
        self.pool_fd = -1
        self.pool_mmap = None
        self.pool_size = 0

        # 缓存相关
        self.cache: Dict[int, str] = {}
        self.lru_list: List[Tuple[int, str]] = []
        self.max_cache_size = 1000
        self.cache_hits = 0
        self.cache_misses = 0

        # 快速缓存（无锁）
        self.fast_cache: List[Tuple[int, str]] = [(0, "")] * 16  # 16个条目的快速缓存
        self.fast_cache_index = 0

        # 线程安全
        self.mutex = threading.Lock()

    def get_string_id(self, string: str) -> int:
        """获取字符串ID（如果不存在则添加）"""
        with self.mutex:
            # 在mmap模式下，不允许添加新字符串
            if self.use_mmap_mode:
                raise RuntimeError("字符串池处于mmap模式，不允许添加新字符串")

            if string in self.string_to_id:
                return self.string_to_id[string]

            # 新字符串，添加到池中
            new_id = len(self.string_table)
            self.string_to_id[string] = new_id
            self.string_table.append(string)
            self.string_count = len(self.string_table)
            self.total_size += self._calculate_string_size(string)

            return new_id

    def get_string(self, string_id: int) -> str:
        """根据ID获取字符串，与C++版本完全一致"""
        # 1. 先检查无锁快速缓存（最快路径）
        fast_result = self._get_string_from_fast_cache(string_id)
        if fast_result:
            self.cache_hits += 1
            return fast_result

        # 2. 检查主缓存（需要锁）
        with self.mutex:
            if string_id in self.cache:
                # 更新LRU列表
                result = self.cache[string_id]
                self._update_lru(string_id, result)

                # 同时更新快速缓存
                self._update_fast_cache(string_id, result)

                self.cache_hits += 1
                return result

            self.cache_misses += 1

            # 3. 根据模式选择读取方式
            if self.use_mmap_mode:
                # 内存映射模式：从mmap中按需读取
                if string_id < self.string_count:
                    result = self._parse_string_from_mmap(string_id)
                    self._update_cache(string_id, result)
                    self._update_fast_cache(string_id, result)  # 同时更新快速缓存
                    return result
            else:
                # 传统模式：从内存中读取
                if string_id < len(self.string_table):
                    result = self.string_table[string_id]
                    self._update_cache(string_id, result)
                    self._update_fast_cache(string_id, result)  # 同时更新快速缓存
                    return result

            return ""

    def serialize(self) -> bytes:
        """序列化字符串池，与C++版本完全一致"""
        with self.mutex:
            # 紧凑格式：使用变长编码优化存储
            data = bytearray()

            # 写入字符串数量 (变长编码)
            count = len(self.string_table)
            self._encode_varint(data, count)

            # 写入每个字符串 - 使用紧凑格式
            for string in self.string_table:
                # 使用变长编码存储字符串长度（注意：必须是UTF-8编码后的字节长度）
                encoded_string = string.encode("utf-8")
                length = len(encoded_string)
                self._encode_varint(data, length)

                # 直接写入字符串内容（与C++版本一致，使用UTF-8编码）
                data.extend(encoded_string)

            return bytes(data)

    def deserialize(self, data: bytes) -> None:
        """反序列化字符串池，与C++版本完全一致"""
        with self.mutex:
            self.clear()

            if len(data) < 1:  # 至少需要1字节的字符串数量
                return

            offset = 0

            # 读取字符串数量 (变长编码)
            count, offset = self._decode_varint(data, offset)

            self.string_count = count

            # 预分配内存以提高性能
            # Python的list和dict没有reserve方法，使用预分配
            self.string_table = [None] * count
            self.string_to_id = {}
            self.string_offsets = [None] * count

            # 读取每个字符串
            for i in range(count):
                if offset >= len(data):
                    break

                # 记录字符串的偏移量
                self.string_offsets.append(offset)

                # 读取字符串长度 (变长编码)
                length, offset = self._decode_varint(data, offset)

                if offset + length > len(data):
                    break

                # 使用更高效的字符串构造
                string = data[offset : offset + length].decode("utf-8")

                self.string_to_id[string] = i
                self.string_table.append(string)
                self.total_size += self._get_varint_size(length) + length

                offset += length

    def clear(self) -> None:
        """清空池"""
        with self.mutex:
            self._cleanup_mmap()
            self.string_to_id.clear()
            self.string_table.clear()
            self.string_offsets.clear()
            self.cache.clear()
            self.lru_list.clear()
            self.total_size = 0
            self.string_count = 0
            self.use_mmap_mode = False

    def get_pool_size(self) -> int:
        """获取池中唯一字符串数量"""
        if self.use_mmap_mode:
            return self.string_count
        return len(self.string_table)

    def get_total_size(self) -> int:
        """获取总大小"""
        return self.total_size

    def load_from_file(self, file_path: str) -> bool:
        """从文件加载字符串池（使用mmap），与C++版本完全一致"""
        with self.mutex:
            self._cleanup_mmap()

            # 打开文件
            try:
                self.pool_fd = os.open(file_path, os.O_RDONLY)
            except OSError as e:
                print(f"无法打开字符串池文件: {file_path} (错误: {e})")
                return False

            # 获取文件大小
            try:
                stat_info = os.stat(file_path)
                self.pool_size = stat_info.st_size
            except OSError as e:
                print(f"无法获取字符串池文件大小: {file_path} (错误: {e})")
                os.close(self.pool_fd)
                self.pool_fd = -1
                return False

            if self.pool_size == 0:
                print(f"字符串池文件为空: {file_path}")
                os.close(self.pool_fd)
                self.pool_fd = -1
                return False

            # 内存映射
            try:
                self.pool_mmap = mmap.mmap(
                    self.pool_fd, self.pool_size, access=mmap.ACCESS_READ
                )
            except OSError as e:
                print(f"无法映射字符串池文件: {file_path} (错误: {e})")
                os.close(self.pool_fd)
                self.pool_fd = -1
                self.pool_mmap = None
                return False

            # 读取字符串数量 (变长编码)
            if self.pool_size >= 1:  # 至少需要1字节的变长编码
                offset = 0
                self.string_count, offset = self._decode_varint_from_mmap(offset)
                print(
                    f"字符串池mmap成功: {file_path} (大小: {self.pool_size} 字节, 字符串数: {self.string_count})"
                )
            else:
                print(f"字符串池文件格式错误: {file_path} (文件太小)")
                self._cleanup_mmap()
                return False

            # 尝试从预构建的索引文件加载偏移量索引
            index_file_path = file_path + ".index"
            if not self._load_offsets_index_from_file(index_file_path):
                # 如果预构建索引不存在，则构建索引
                print("预构建索引不存在，开始构建偏移量索引...")
                self._build_string_offsets_index()

                # 保存索引到文件以供下次使用
                self._save_offsets_index_to_file(index_file_path)
            else:
                print("成功从预构建索引文件加载偏移量索引")

            # 验证偏移量索引是否正确构建
            if len(self.string_offsets) != self.string_count:
                print(
                    f"警告: 偏移量索引长度不匹配 (期望: {self.string_count}, 实际: {len(self.string_offsets)})"
                )
                print("重新构建偏移量索引...")
                self._build_string_offsets_index()

            self.use_mmap_mode = True
            return True

    def save_to_file(self, file_path: str) -> bool:
        """保存字符串池到文件，与C++版本完全一致"""
        with self.mutex:
            if self.use_mmap_mode:
                return False
            else:
                # 传统模式下，序列化到文件
                # 注意：这里不能调用 self.serialize()，因为会导致死锁
                # 直接在锁内进行序列化操作
                data = bytearray()

                # 写入字符串数量 (变长编码)
                count = len(self.string_table)
                self._encode_varint(data, count)

                # 写入每个字符串 - 使用紧凑格式
                for string in self.string_table:
                    # 使用变长编码存储字符串长度（注意：必须是UTF-8编码后的字节长度）
                    encoded_string = string.encode("utf-8")
                    length = len(encoded_string)
                    self._encode_varint(data, length)

                    # 直接写入字符串内容
                    data.extend(encoded_string)

                serialized_data = bytes(data)

                try:
                    with open(file_path, "wb") as f:
                        f.write(serialized_data)

                    # 在保存 .pool 文件后，构建并保存 .pool.index 文件
                    if not self.string_table:
                        return True

                    index_file_path = file_path + ".index"

                    # 检查索引文件是否已存在，如果不存在才创建
                    if not os.path.exists(index_file_path):
                        # 构建偏移量索引
                        self._build_offsets_index_from_memory()

                        if not self._save_offsets_index_to_file(index_file_path):
                            print(f"警告: 无法保存字符串池索引文件: {index_file_path}")
                        else:
                            print(f"已创建字符串池索引文件: {index_file_path}")
                    else:
                        print(f"字符串池索引文件已存在，跳过创建: {index_file_path}")

                    return True
                except IOError as e:
                    print(f"无法保存字符串池文件: {file_path} (错误: {e})")
                    return False

    def _calculate_string_size(self, string: str) -> int:
        """计算字符串在池中的存储大小：长度(变长编码) + 字符串内容"""
        return self._get_varint_size(len(string)) + len(string.encode("utf-8"))

    def _parse_string_from_mmap(self, string_id: int) -> str:
        """从mmap中解析字符串，与C++版本完全一致"""
        if (
            not self.pool_mmap
            or string_id >= self.string_count
            or string_id >= len(self.string_offsets)
        ):
            return ""

        # 使用预计算的偏移量索引，实现O(1)查找
        offset = self.string_offsets[string_id]

        # 检查偏移量是否在有效范围内
        if offset + 1 > self.pool_size:  # 至少需要1字节的变长编码
            print(
                f"警告: 字符串池mmap数据损坏，偏移超出范围 (id={string_id}, offset={offset}, pool_size={self.pool_size})"
            )
            return ""

        # 读取字符串长度 (变长编码)
        length, offset = self._decode_varint_from_mmap(offset)

        # 检查长度是否合理
        if length > self.pool_size or offset + length > self.pool_size:
            print(
                f"警告: 字符串长度异常 (id={string_id}, length={length}, pool_size={self.pool_size})"
            )
            return ""

        try:
            return self.pool_mmap[offset : offset + length].decode("utf-8")
        except UnicodeDecodeError as e:
            print(
                f"警告: 字符串解码失败 (id={string_id}, offset={offset}, length={length}): {e}"
            )
            # 尝试使用错误处理策略
            return self.pool_mmap[offset : offset + length].decode(
                "utf-8", errors="replace"
            )

    def _build_string_offsets_index(self) -> None:
        """构建字符串偏移量索引，与C++版本完全一致"""
        if not self.pool_mmap or self.string_count == 0:
            return

        # 清空现有的偏移量索引
        self.string_offsets.clear()
        # Python的list没有reserve方法，使用预分配
        self.string_offsets = [0] * self.string_count

        # 跳过字符串数量字段 (变长编码)
        offset = 0
        count, offset = self._decode_varint_from_mmap(offset)

        # 遍历所有字符串，构建偏移量索引
        for i in range(self.string_count):
            if offset + 1 > self.pool_size:  # 至少需要1字节的变长编码
                print(
                    f"警告: 字符串池数据损坏，偏移超出范围 (i={i}, offset={offset}, pool_size={self.pool_size})"
                )
                break

            # 记录当前字符串的偏移量
            self.string_offsets[i] = offset

            # 读取字符串长度 (变长编码)
            if offset >= self.pool_size:
                print(
                    f"警告: 字符串池数据损坏，偏移超出范围 (i={i}, offset={offset}, pool_size={self.pool_size})"
                )
                break

            length, new_offset = self._decode_varint_from_mmap(offset)
            if new_offset == -1:
                print(
                    f"警告: 无法解析字符串长度 (i={i}, offset={offset}, pool_size={self.pool_size})"
                )
                break
            offset = new_offset

            # 跳过字符串内容
            if offset + length > self.pool_size:
                print(
                    f"警告: 字符串长度异常 (i={i}, length={length}, offset={offset}, pool_size={self.pool_size})"
                )
                break
            offset += length

        print(f"字符串偏移量索引构建完成: {len(self.string_offsets)} 个字符串")

    def _build_offsets_index_from_memory(self) -> None:
        """从内存构建偏移量索引，与C++版本完全一致"""
        if not self.string_table:
            return

        # 清空现有的偏移量索引
        self.string_offsets.clear()
        # Python的list没有reserve方法，使用预分配
        self.string_offsets = [0] * len(self.string_table)

        # 计算每个字符串在序列化数据中的偏移量
        current_offset = 0

        # 跳过字符串数量字段 (变长编码)
        # 计算字符串数量字段的字节数
        count = len(self.string_table)
        current_offset += self._get_varint_size(count)

        # 为每个字符串计算偏移量
        for i in range(len(self.string_table)):
            self.string_offsets[i] = current_offset

            # 计算这个字符串在序列化数据中占用的字节数
            length = len(self.string_table[i])
            current_offset += self._get_varint_size(length) + length

        # 更新字符串数量
        self.string_count = len(self.string_table)

        print(f"从内存构建字符串偏移量索引完成: {len(self.string_offsets)} 个字符串")

    def _save_offsets_index_to_file(self, index_file_path: str) -> bool:
        """保存偏移量索引到文件，与C++版本完全一致"""
        if not self.string_offsets:
            return False

        try:
            with open(index_file_path, "wb") as f:
                # 写入字符串数量
                f.write(struct.pack("I", self.string_count))

                # 写入所有偏移量
                for offset in self.string_offsets:
                    f.write(struct.pack("Q", offset))  # 使用8字节存储偏移量

            print(f"偏移量索引已保存到: {index_file_path}")
            return True
        except IOError as e:
            print(f"无法保存偏移量索引文件: {index_file_path} (错误: {e})")
            return False

    def _load_offsets_index_from_file(self, index_file_path: str) -> bool:
        """从文件加载偏移量索引，与C++版本完全一致"""
        try:
            with open(index_file_path, "rb") as f:
                # 读取字符串数量
                file_string_count = struct.unpack("I", f.read(4))[0]

                # 验证字符串数量是否匹配
                if file_string_count != self.string_count:
                    print(
                        f"索引文件中的字符串数量不匹配: {file_string_count} != {self.string_count}"
                    )
                    return False

                # 读取偏移量数据
                self.string_offsets.clear()
                # Python的list没有reserve方法，使用预分配
                self.string_offsets = [0] * self.string_count
                for i in range(self.string_count):
                    offset = struct.unpack("Q", f.read(8))[0]
                    self.string_offsets[i] = offset

            return True
        except IOError:
            return False

    def _get_string_from_fast_cache(self, string_id: int) -> str:
        """无锁快速缓存查找"""
        for i in range(len(self.fast_cache)):
            if self.fast_cache[i][0] == string_id:
                return self.fast_cache[i][1]
        return ""

    def _update_fast_cache(self, string_id: int, value: str) -> None:
        """使用原子操作更新快速缓存"""
        index = self.fast_cache_index % len(self.fast_cache)
        self.fast_cache[index] = (string_id, value)
        self.fast_cache_index += 1

    def _update_cache(self, string_id: int, value: str) -> None:
        """更新LRU缓存"""
        # 如果缓存已满，移除最久未使用的条目
        if len(self.cache) >= self.max_cache_size:
            if self.lru_list:
                old_id, _ = self.lru_list.pop()
                del self.cache[old_id]

        # 添加新条目到缓存
        self.cache[string_id] = value
        self.lru_list.append((string_id, value))

    def _update_lru(self, string_id: int, value: str) -> None:
        """更新LRU列表"""
        # 移除旧条目
        self.lru_list = [(id, val) for id, val in self.lru_list if id != string_id]
        # 添加到末尾
        self.lru_list.append((string_id, value))

    def _cleanup_mmap(self) -> None:
        """清理内存映射"""
        if self.pool_mmap:
            self.pool_mmap.close()
            self.pool_mmap = None
        if self.pool_fd >= 0:
            os.close(self.pool_fd)
            self.pool_fd = -1
        self.pool_size = 0

    def _encode_varint(self, data: bytearray, value: int) -> None:
        """变长编码，与C++版本完全一致"""
        # 使用变长编码，每个字节的最高位表示是否还有后续字节
        while value >= 0x80:
            data.append((value & 0xFF) | 0x80)
            value >>= 7
        data.append(value & 0xFF)

    def _get_varint_size(self, value: int) -> int:
        """计算变长编码的字节数"""
        size = 1
        while value >= 0x80:
            value >>= 7
            size += 1
        return size

    def _decode_varint(self, data: bytes, offset: int) -> Tuple[int, int]:
        """变长解码，与C++版本完全一致"""
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

    def _decode_varint_from_mmap(self, offset: int) -> Tuple[int, int]:
        """从mmap中解码变长整数，与C++版本完全一致"""
        if offset >= self.pool_size:
            return -1, -1

        value = 0
        shift = 0
        start_offset = offset

        while offset < self.pool_size:
            byte = self.pool_mmap[offset]
            offset += 1
            value |= (byte & 0x7F) << shift

            if (byte & 0x80) == 0:
                break  # 最后一个字节

            shift += 7
            if shift >= 32:
                print(f"警告: 变长编码值过大 (offset={start_offset}, value={value})")
                return -1, -1

        # 如果循环结束但没有找到结束字节，返回错误
        if offset >= self.pool_size and (self.pool_mmap[offset - 1] & 0x80) != 0:
            print(f"警告: 变长编码不完整 (offset={start_offset})")
            return -1, -1

        return value, offset

    def get_cache_stats(self) -> Dict:
        """获取缓存统计信息"""
        with self.mutex:
            total_requests = self.cache_hits + self.cache_misses
            hit_rate = self.cache_hits / total_requests if total_requests > 0 else 0.0

            return {
                "hits": self.cache_hits,
                "misses": self.cache_misses,
                "cache_size": len(self.cache),
                "hit_rate": hit_rate,
            }
