import os
import struct
import threading
from typing import Dict, List, Optional

from .geometry_data import GeometryData
from .geometry_serializer import GeometrySerializer


class GeometryStorage:
    """几何数据存储类 - 支持分块索引和按需加载，与C++版本兼容"""

    class IndexChunk:
        """索引块结构，与C++版本完全一致"""

        def __init__(self):
            self.start_fid = 0
            self.end_fid = 0
            self.offset_map: Dict[int, int] = {}
            self.loaded = False

    def __init__(self, geometry_file: str):
        self.geometry_file = geometry_file

        # 分块索引
        self.index_chunks: List[GeometryStorage.IndexChunk] = []
        self.use_chunked_mode = False
        self.chunk_size = 10000  # 默认每个块10000个要素

        # 简单缓存 - 只缓存最近访问的数据
        self.cache: Dict[int, GeometryData] = {}
        self.max_cache_size = 1000  # 默认缓存1000个几何

        # 线程安全
        self.mutex = threading.Lock()

        # 创建目录
        os.makedirs(os.path.dirname(geometry_file), exist_ok=True)

    def write_geometry(self, geometry: GeometryData) -> int:
        """写入几何数据，与C++版本完全一致"""
        try:
            with open(self.geometry_file, "ab") as file:
                offset = file.tell()
                geom_binary = GeometrySerializer.serialize_geometry(geometry)

                if geom_binary:
                    file.write(geom_binary)
                    file.flush()
                    # 清除缓存以确保数据一致性
                    with self.mutex:
                        self.cache.clear()
                    return offset
                else:
                    raise RuntimeError(
                        f"几何数据序列化失败 for FID {geometry.get_feature_id()}"
                    )
        except IOError as e:
            raise RuntimeError(
                f"无法打开几何文件进行写入: {self.geometry_file} (错误: {e})"
            )

    def read_geometry(self, feature_id: int) -> GeometryData:
        """读取几何数据 - 优化版本，支持按需加载，与C++版本完全一致"""
        # 检查缓存
        with self.mutex:
            if feature_id in self.cache:
                return self.cache[feature_id]

        # 获取偏移量
        offset = self._get_chunked_offset(feature_id)
        if offset < 0:
            # 如果没有分块索引，使用顺序搜索
            geometry = self._sequential_search_geometry(feature_id)
        else:
            # 读取几何数据
            geometry = self._read_geometry_at_offset(feature_id, offset)

        # 更新缓存
        with self.mutex:
            self._update_cache(feature_id, geometry)

        return geometry

    def read_geometry_on_demand(self, feature_id: int) -> GeometryData:
        """按需读取几何数据（不依赖完整索引），与C++版本完全一致"""
        # 首先尝试使用分块索引
        offset = self._get_chunked_offset(feature_id)
        if offset >= 0:
            return self._read_geometry_at_offset(feature_id, offset)

        # 否则进行顺序搜索
        return self._sequential_search_geometry(feature_id)

    def read_geometry_at_offset(self, feature_id: int, offset: int) -> GeometryData:
        """根据偏移读取几何数据，与C++版本完全一致"""
        return self._read_geometry_at_offset(feature_id, offset)

    def has_feature(self, feature_id: int) -> bool:
        """检查要素是否存在，与C++版本完全一致"""
        try:
            self.read_geometry(feature_id)
            return True
        except (ValueError, RuntimeError):
            return False

    def get_all_feature_ids(self) -> List[int]:
        """获取所有要素ID，与C++版本完全一致"""
        if not os.path.exists(self.geometry_file):
            return []

        # 构建偏移索引
        offsets = self._build_offset_index()
        return list(offsets.keys())

    def clear_cache(self) -> None:
        """清除缓存，与C++版本完全一致"""
        with self.mutex:
            self.cache.clear()

    def get_geometry_file_path(self) -> str:
        """获取文件路径，与C++版本完全一致"""
        return self.geometry_file

    def set_chunk_size(self, chunk_size: int) -> None:
        """设置分块大小，与C++版本完全一致"""
        self.chunk_size = chunk_size

    def set_cache_size(self, cache_size: int) -> None:
        """设置缓存大小，与C++版本完全一致"""
        self.max_cache_size = cache_size

    def build_chunked_index(self) -> None:
        """构建分块索引，与C++版本完全一致"""
        if not os.path.exists(self.geometry_file):
            return

        self.index_chunks.clear()
        current_chunk = None

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

                # 检查是否需要创建新的分块
                if (
                    current_chunk is None
                    or len(current_chunk.offset_map) >= self.chunk_size
                ):
                    if current_chunk is not None:
                        current_chunk.end_fid = feature_id - 1
                        self.index_chunks.append(current_chunk)

                    current_chunk = GeometryStorage.IndexChunk()
                    current_chunk.start_fid = feature_id

                current_chunk.offset_map[feature_id] = current_offset

        # 添加最后一个分块
        if current_chunk is not None:
            if current_chunk.offset_map:
                current_chunk.end_fid = max(current_chunk.offset_map.keys())
            self.index_chunks.append(current_chunk)

        self.use_chunked_mode = True
        print(f"构建分块索引完成: {len(self.index_chunks)} 个分块")

    def save_chunked_index(self, index_file: str) -> None:
        """保存分块索引到文件，使用二进制格式与C++版本完全一致"""
        if not self.use_chunked_mode or not self.index_chunks:
            print("没有分块索引需要保存")
            return

        try:
            with open(index_file, "wb") as f:
                # 写入文件头 (24字节)
                # version (4字节) + chunk_count (4字节) + chunk_size (8字节) + reserved (8字节)
                version = 1
                chunk_count = len(self.index_chunks)
                chunk_size = self.chunk_size
                reserved = 0
                
                f.write(struct.pack("I", version))  # uint32_t
                f.write(struct.pack("I", chunk_count))  # uint32_t
                f.write(struct.pack("Q", chunk_size))  # uint64_t
                f.write(struct.pack("Q", reserved))  # uint64_t

                # 写入每个索引块
                for chunk in self.index_chunks:
                    # 写入块头 (24字节)
                    # start_fid (8字节) + end_fid (8字节) + entry_count (4字节) + reserved (4字节)
                    entry_count = len(chunk.offset_map)
                    
                    f.write(struct.pack("Q", chunk.start_fid))  # uint64_t
                    f.write(struct.pack("Q", chunk.end_fid))  # uint64_t
                    f.write(struct.pack("I", entry_count))  # uint32_t
                    f.write(struct.pack("I", 0))  # uint32_t reserved

                    # 写入偏移映射 (每个条目16字节)
                    # feature_id (8字节) + offset (8字节)
                    for fid, offset in chunk.offset_map.items():
                        f.write(struct.pack("Q", fid))  # uint64_t
                        f.write(struct.pack("q", offset))  # int64_t

            print(f"分块索引已保存到: {index_file}")
        except IOError as e:
            print(f"无法保存分块索引文件: {index_file} (错误: {e})")

    def load_chunked_index(self, index_file: str) -> None:
        """从文件加载分块索引，使用二进制格式与C++版本完全一致"""
        if not os.path.exists(index_file):
            print(f"分块索引文件不存在: {index_file}")
            return

        try:
            with open(index_file, "rb") as f:
                # 读取文件头 (24字节)
                header_data = f.read(24)
                if len(header_data) < 24:
                    print("分块索引文件格式不正确：文件头不完整")
                    return
                
                version, chunk_count, chunk_size, reserved = struct.unpack("IIQQ", header_data)
                
                if version != 1:
                    print(f"不支持的分块索引版本: {version}")
                    return

                self.chunk_size = chunk_size
                self.index_chunks.clear()

                # 读取每个索引块
                for _ in range(chunk_count):
                    # 读取块头 (24字节)
                    chunk_header_data = f.read(24)
                    if len(chunk_header_data) < 24:
                        print("分块索引文件格式不正确：块头不完整")
                        break
                    
                    start_fid, end_fid, entry_count, reserved = struct.unpack("QQII", chunk_header_data)
                    
                    chunk = GeometryStorage.IndexChunk()
                    chunk.start_fid = start_fid
                    chunk.end_fid = end_fid
                    chunk.loaded = True
                    
                    # 读取偏移映射
                    for _ in range(entry_count):
                        entry_data = f.read(16)
                        if len(entry_data) < 16:
                            print("分块索引文件格式不正确：条目不完整")
                            break
                        
                        fid, offset = struct.unpack("Qq", entry_data)
                        chunk.offset_map[fid] = offset
                    
                    self.index_chunks.append(chunk)

                self.use_chunked_mode = True
                print(f"分块索引已加载: {len(self.index_chunks)} 个分块")

        except IOError as e:
            print(f"加载分块索引失败: {e}")

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

    def _update_cache(self, fid: int, data: GeometryData) -> None:
        """更新LRU缓存，与C++版本完全一致"""
        if len(self.cache) >= self.max_cache_size:
            # 简单的LRU：删除第一个条目
            first_key = next(iter(self.cache))
            del self.cache[first_key]

        # 添加新条目到缓存
        self.cache[fid] = data

    def _read_geometry_at_offset(self, feature_id: int, offset: int) -> GeometryData:
        """根据偏移量读取几何数据，与C++版本完全一致"""
        try:
            with open(self.geometry_file, "rb") as f:
                f.seek(offset)

                # 读取完整的数据记录
                # 先读取前56字节（包含coord_size字段）
                header_data = f.read(56)
                if len(header_data) < 56:
                    raise ValueError(f"几何数据不完整 for FID {feature_id}")

                # 解析坐标数据长度
                coord_size = struct.unpack("I", header_data[52:56])[0]

                # 读取坐标数据
                coord_data = f.read(coord_size)
                if len(coord_data) < coord_size:
                    raise ValueError(f"几何坐标数据不完整 for FID {feature_id}")

                # 组合所有数据进行反序列化
                geom_data = header_data + coord_data
                geometry = GeometrySerializer.deserialize_geometry(geom_data)

                return geometry

        except IOError as e:
            raise RuntimeError(f"读取几何数据失败: {e}")

    def _sequential_search_geometry(self, feature_id: int) -> GeometryData:
        """顺序搜索几何数据，与C++版本完全一致"""
        if not os.path.exists(self.geometry_file):
            raise ValueError(f"几何文件不存在: {self.geometry_file}")

        with open(self.geometry_file, "rb") as f:
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
                    return self._read_geometry_at_offset(feature_id, current_offset)

                # 跳过当前记录
                # 跳过geometry_type(1) + padding(7) + bbox(32) + num_rings(4)
                f.seek(44, 1)

                # 读取坐标数据大小
                coord_size_data = f.read(4)
                if len(coord_size_data) < 4:
                    break
                coord_size = struct.unpack("I", coord_size_data)[0]

                # 跳过坐标数据
                f.seek(coord_size, 1)

        raise ValueError(f"Feature ID {feature_id} not found")

    def _build_offset_index(self) -> Dict[int, int]:
        """构建偏移索引"""
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
