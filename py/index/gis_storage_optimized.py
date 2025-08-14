# gis_storage_optimized.py
import struct
import os
import json
from typing import List, Dict, Tuple, Any
from s2sphere import LatLng, LatLngRect, RegionCoverer
import pickle
from collections import defaultdict


class GeometryData:
    """几何数据结构"""

    def __init__(self, feature_id: int, geometry_type: int, coordinates: bytes,
                 bbox: Tuple[float, float, float,
                             float], s2_cell_ids: List[int]):
        self.feature_id = feature_id
        self.geometry_type = geometry_type  # 0=Point, 1=Line, 2=Polygon等
        self.coordinates = coordinates
        self.bbox = bbox
        self.s2_cell_ids = s2_cell_ids  # 存储覆盖的S2单元格ID列表


class AttributeData:
    """属性数据结构"""

    def __init__(self, feature_id: int, properties: Dict[str, Any]):
        self.feature_id = feature_id
        self.properties = properties


class S2IndexEntry:
    """S2索引条目"""

    def __init__(self, cell_id: int, offset: int, size: int):
        self.cell_id = cell_id
        self.offset = offset
        self.size = size


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
        # 格式: feature_id(8) + geometry_type(1) + bbox(32) + s2_cell_count(4) + s2_cell_ids + coord_size(4) + coords
        data = struct.pack('QbddddQ', geometry.feature_id,
                           geometry.geometry_type, geometry.bbox[0],
                           geometry.bbox[1], geometry.bbox[2],
                           geometry.bbox[3], len(geometry.s2_cell_ids))

        # 写入S2单元格ID
        for cell_id in geometry.s2_cell_ids:
            data += struct.pack('Q', cell_id)

        # 写入坐标数据
        coord_size = len(geometry.coordinates)
        data += struct.pack('I', coord_size)
        data += geometry.coordinates
        return data

    @staticmethod
    def deserialize_geometry(data: bytes) -> Tuple[GeometryData, int]:
        """从二进制数据反序列化几何对象"""
        feature_id, geometry_type, min_x, min_y, max_x, max_y, cell_count = \
            struct.unpack('QbddddQ', data[:45])

        # 读取S2单元格ID
        s2_cell_ids = []
        offset = 45
        for _ in range(cell_count):
            cell_id = struct.unpack('Q', data[offset:offset + 8])[0]
            s2_cell_ids.append(cell_id)
            offset += 8

        # 读取坐标数据
        coord_size = struct.unpack('I', data[offset:offset + 4])[0]
        offset += 4
        coordinates = data[offset:offset + coord_size]

        geometry = GeometryData(feature_id, geometry_type, coordinates,
                                (min_x, min_y, max_x, max_y), s2_cell_ids)

        return geometry, offset + coord_size


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


class CustomS2SpatialIndex:
    """基于您已有实现的自定义S2空间索引"""

    def __init__(self,
                 data_path,
                 index_file='./index/custom_s2.pkl',
                 resolution=15):
        self.data_path = data_path
        self.index_file = index_file
        self.resolution = resolution
        self.s2_index = defaultdict(list)
        self.feature_bounds = {}  # 存储要素的外包矩形用于精确验证
        self.feature_count = 0

        # 创建索引目录
        os.makedirs(os.path.dirname(index_file), exist_ok=True)

        if os.path.exists(index_file):
            print("加载已有的 S2 索引...")
            self.load_index()
        else:
            print("S2 索引文件不存在，请调用 build_index() 构建索引")

    def _calculate_s2_cells(
            self, bbox: Tuple[float, float, float, float]) -> List[int]:
        """计算几何对象覆盖的S2单元格"""
        min_lon, min_lat, max_lon, max_lat = bbox

        # 构建查询区域的 S2 单元格
        p1 = LatLng.from_degrees(min_lat, min_lon)
        p2 = LatLng.from_degrees(max_lat, max_lon)
        query_rect = LatLngRect.from_point_pair(p1, p2)

        coverer = RegionCoverer()
        coverer.min_level = self.resolution
        coverer.max_level = self.resolution
        cell_ids = coverer.get_covering(query_rect)

        return [cell.id() for cell in cell_ids]

    def build_index(self, geometry_file: str):
        """基于二进制几何文件构建S2索引"""
        print("开始构建 S2 索引...")

        self.s2_index.clear()
        self.feature_bounds = {}
        feature_count = 0

        # 从二进制几何文件中读取所有要素并构建索引
        with open(geometry_file, 'rb') as f:
            while True:
                # 读取feature_id和geometry_type
                header_data = f.read(9)  # 8字节feature_id + 1字节geometry_type
                if len(header_data) < 9:
                    break

                feature_id, geometry_type = struct.unpack('Qb', header_data)

                # 读取边界框
                bbox_data = f.read(32)  # 4个double值
                min_x, min_y, max_x, max_y = struct.unpack('dddd', bbox_data)
                bbox = (min_x, min_y, max_x, max_y)
                self.feature_bounds[feature_id] = bbox

                # 读取S2单元格数量
                cell_count_data = f.read(8)  # Q格式
                cell_count = struct.unpack('Q', cell_count_data)[0]

                # 读取S2单元格ID
                s2_cell_ids = []
                for _ in range(cell_count):
                    cell_id_data = f.read(8)
                    cell_id = struct.unpack('Q', cell_id_data)[0]
                    s2_cell_ids.append(cell_id)

                # 读取坐标数据大小和坐标数据
                coord_size_data = f.read(4)
                coord_size = struct.unpack('I', coord_size_data)[0]
                f.read(coord_size)  # 跳过坐标数据

                # 将要素ID添加到对应的S2单元格中
                for cell_id in s2_cell_ids:
                    self.s2_index[cell_id].append(feature_id)

                feature_count += 1

        self.feature_count = feature_count

        # 保存索引
        self.save_index()
        print(f"S2索引构建完成，共处理 {feature_count} 个要素")

    def query_by_bbox(self, bbox: Tuple[float, float, float,
                                        float]) -> List[int]:
        """基于 BBox 查询要素"""
        min_lon, min_lat, max_lon, max_lat = bbox

        # 构建查询区域的 S2 单元格
        p1 = LatLng.from_degrees(min_lat, min_lon)
        p2 = LatLng.from_degrees(max_lat, max_lon)
        query_rect = LatLngRect.from_point_pair(p1, p2)

        coverer = RegionCoverer()
        coverer.min_level = self.resolution
        query_cells = coverer.get_covering(query_rect)

        # 获取候选要素
        candidate_fids = set()
        for cell in query_cells:
            candidate_fids.update(self.s2_index.get(cell.id(), []))

        return list(candidate_fids)

    def load_index(self):
        """从pickle文件加载S2索引"""
        with open(self.index_file, 'rb') as f:
            loaded_data = pickle.load(f)
            if isinstance(loaded_data, dict) and 's2_index' in loaded_data:
                self.s2_index = defaultdict(list, loaded_data['s2_index'])
                self.feature_bounds = loaded_data.get('feature_bounds', {})
                self.feature_count = loaded_data.get('feature_count', 0)
            else:
                raise ValueError("加载的S2索引格式不正确")

        print("S2索引加载完成")

    def save_index(self):
        """将S2索引保存为pickle文件"""
        data_to_save = {
            's2_index': dict(self.s2_index),
            'feature_bounds': self.feature_bounds,
            'feature_count': self.feature_count
        }

        with open(self.index_file, 'wb') as f:
            pickle.dump(data_to_save, f)

        print("S2索引保存完成")


class EfficientGisStorage:
    """高效GIS存储系统"""

    def __init__(self, base_dir: str):
        self.base_dir = base_dir
        self.geometry_file = os.path.join(base_dir, "geom.dat")
        self.attribute_file = os.path.join(base_dir, "attr.dat")

        # 创建目录
        os.makedirs(base_dir, exist_ok=True)

    def _calculate_bbox(
        self, coordinates: List[Tuple[float, float]]
    ) -> Tuple[float, float, float, float]:
        """计算边界框"""
        if not coordinates:
            return (0, 0, 0, 0)

        min_x = min(coord[0] for coord in coordinates)
        max_x = max(coord[0] for coord in coordinates)
        min_y = min(coord[1] for coord in coordinates)
        max_y = max(coord[1] for coord in coordinates)
        return (min_x, min_y, max_x, max_y)

    def convert_shapefile(self, shapefile_path: str, s2_resolution: int = 15):
        """将Shapefile转换为自定义二进制格式"""
        print(f"开始转换Shapefile: {shapefile_path}")

        # 打开输出文件
        geom_out = open(self.geometry_file, 'wb')
        attr_out = open(self.attribute_file, 'wb')

        # 读取Shapefile
        reader = shapefile.Reader(shapefile_path)
        fields = reader.fields[1:]  # 跳过删除标记字段
        field_names = [field[0] for field in fields]

        feature_count = 0
        for i, record in enumerate(reader.iterShapeRecords()):
            shape = record.shape
            attributes = record.record

            # 处理几何数据
            coords = []
            if shape.shapeType in (1, 11, 21):  # Point类型
                coords = [(shape.points[0][0], shape.points[0][1])]
                geom_type = 0
            elif shape.shapeType in (3, 13, 23):  # PolyLine类型
                coords = [(point[0], point[1]) for point in shape.points]
                geom_type = 1
            elif shape.shapeType in (5, 15, 25):  # Polygon类型
                coords = [(point[0], point[1]) for point in shape.points]
                geom_type = 2
            else:
                print(f"跳过不支持的几何类型: {shape.shapeType}")
                continue  # 不支持的几何类型

            # 计算边界框
            bbox = self._calculate_bbox(coords)

            # 序列化坐标
            coord_data = GeometrySerializer.serialize_coordinates(coords)

            # 计算S2单元格
            s2_index = CustomS2SpatialIndex("", resolution=s2_resolution)
            s2_cell_ids = s2_index._calculate_s2_cells(bbox)

            # 创建几何数据对象
            geom_data = GeometryData(i, geom_type, coord_data, bbox,
                                     s2_cell_ids)

            # 写入几何文件
            geom_binary = GeometrySerializer.serialize_geometry(geom_data)
            geom_out.write(geom_binary)

            # 处理属性数据
            props = {}
            for j, field_name in enumerate(field_names):
                props[field_name] = attributes[j]

            attr_data = AttributeData(i, props)
            attr_binary = AttributeSerializer.serialize_attributes(attr_data)
            attr_out.write(attr_binary)

            feature_count += 1

        # 关闭文件
        geom_out.close()
        attr_out.close()

        print(f"转换完成:")
        print(f"- 几何数据: {self.geometry_file}")
        print(f"- 属性数据: {self.attribute_file}")
        print(f"- 总共处理 {feature_count} 个要素")

        # 构建S2索引
        s2_index_file = os.path.join(self.base_dir, "s2_index.pkl")
        s2_index = CustomS2SpatialIndex(self.geometry_file, s2_index_file,
                                        s2_resolution)
        s2_index.build_index(self.geometry_file)

        return s2_index_file

    def query_by_region(self, bbox: Tuple[float, float, float, float],
                        s2_index_file: str) -> List[Dict]:
        """基于区域查询要素"""
        # 加载S2索引
        s2_index = CustomS2SpatialIndex("", s2_index_file)
        s2_index.load_index()

        # 使用S2索引查找候选要素
        candidate_fids = s2_index.query_by_bbox(bbox)

        results = []
        with open(self.geometry_file, 'rb') as geom_file:
            # 需要先建立feature_id到文件偏移的映射
            feature_offsets = self._build_offset_index()

            for fid in candidate_fids:
                if fid not in feature_offsets:
                    continue

                # 定位到几何数据
                offset = feature_offsets[fid]
                geom_file.seek(offset)

                # 读取足够大的数据块（假设最大1MB）
                geom_data = geom_file.read(1024 * 1024)

                # 反序列化几何数据
                try:
                    geometry, _ = GeometrySerializer.deserialize_geometry(
                        geom_data)

                    # 获取坐标
                    coords, _ = GeometrySerializer.deserialize_coordinates(
                        geometry.coordinates)

                    # 构建结果
                    result = {
                        'id': geometry.feature_id,
                        'type': geometry.geometry_type,
                        'coordinates': coords,
                        'bbox': geometry.bbox
                    }
                    results.append(result)
                except Exception as e:
                    print(f"反序列化要素 {fid} 时出错: {e}")

        return results

    def _build_offset_index(self) -> Dict[int, int]:
        """构建feature_id到文件偏移的索引"""
        offsets = {}
        with open(self.geometry_file, 'rb') as f:
            offset = 0
            while True:
                current_pos = f.tell()
                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break

                fid = struct.unpack('Q', fid_data)[0]
                offsets[fid] = current_pos

                # 跳到下一个记录
                # 读取geometry_type
                geom_type_data = f.read(1)
                if len(geom_type_data) < 1:
                    break

                # 读取bbox
                f.read(32)

                # 读取S2单元格数量
                cell_count_data = f.read(8)
                if len(cell_count_data) < 8:
                    break
                cell_count = struct.unpack('Q', cell_count_data)[0]

                # 跳过S2单元格ID
                f.read(8 * cell_count)

                # 读取坐标大小并跳过坐标数据
                coord_size_data = f.read(4)
                if len(coord_size_data) < 4:
                    break
                coord_size = struct.unpack('I', coord_size_data)[0]
                f.read(coord_size)

        return offsets

    def get_attributes(self, feature_id: int) -> Dict:
        """获取指定要素的属性"""
        with open(self.attribute_file, 'rb') as attr_file:
            # 简化实现：顺序查找（实际应用中应建立索引）
            while True:
                # 读取当前位置
                current_pos = attr_file.tell()

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


# 使用示例
if __name__ == "__main__":
    # 1. 转换Shapefile并构建索引
    storage = EfficientGisStorage("./my_gis_data")
    s2_index_file = storage.convert_shapefile("example.shp", s2_resolution=15)

    # 2. 查询要素
    bbox = (100.0, 30.0, 105.0, 35.0
            )  # 查询区域 (min_lon, min_lat, max_lon, max_lat)
    features = storage.query_by_region(bbox, s2_index_file)

    # 3. 获取属性信息
    for feature in features:
        attributes = storage.get_attributes(feature['id'])
        print(f"Feature {feature['id']} attributes: {attributes}")
        print(f"Coordinates: {feature['coordinates']}")
