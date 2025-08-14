# 修改后的converter.py，实现相对坐标存储优化
import os
import pickle
import struct
from typing import Tuple, List
from osgeo import ogr

from gisstorage.index_adapter import S2IndexAdapter
from gisstorage.models import GeometryData, AttributeData
from gisstorage.serializers import GeometrySerializer


class ShapefileConverter:
    """Shapefile转换器"""

    def __init__(self, output_dir: str):

        from gissystem import GeometryStorage, AttributeStorage
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)

        # 初始化存储管理器
        self.geometry_storage = GeometryStorage(
            os.path.join(output_dir, "geom.dat"))
        self.attribute_storage = AttributeStorage(
            os.path.join(output_dir, "attr.dat"))

        # 初始化S2适配器
        self.s2_adapter = S2IndexAdapter()

        # 添加索引文件
        self.index_file = os.path.join(output_dir, "index.dat")

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

    def _encode_coordinates_delta(
            self, coordinates: List[Tuple[float, float]]) -> bytes:
        """
        使用差分编码压缩坐标数据
        第一个点存储绝对坐标，后续点存储相对于前一个点的偏移量
        """
        if not coordinates:
            return b''

        # 第一个点存储绝对坐标（double精度）
        encoded_data = struct.pack('dd', coordinates[0][0], coordinates[0][1])

        # 后续点存储相对于前一个点的偏移量
        for i in range(1, len(coordinates)):
            dx = coordinates[i][0] - coordinates[i - 1][0]
            dy = coordinates[i][1] - coordinates[i - 1][1]
            # 使用float存储偏移量（节省空间，通常足够精度）
            encoded_data += struct.pack('ff', dx, dy)

        return encoded_data

    def _decode_coordinates_delta(self,
                                  data: bytes) -> List[Tuple[float, float]]:
        """
        解码差分编码的坐标数据
        """
        if not data:
            return []

        coordinates = []

        # 读取第一个点的绝对坐标（16字节）
        if len(data) >= 16:
            x, y = struct.unpack('dd', data[:16])
            coordinates.append((x, y))

            # 读取后续点的偏移量（每8字节）
            offset_pos = 16
            while offset_pos < len(data):
                if len(data) >= offset_pos + 8:
                    dx, dy = struct.unpack('ff',
                                           data[offset_pos:offset_pos + 8])
                    # 重构绝对坐标
                    prev_x, prev_y = coordinates[-1]
                    coordinates.append((prev_x + dx, prev_y + dy))
                    offset_pos += 8
                else:
                    break

        return coordinates

    def _encode_coordinates_delta_optimized(
            self, coordinates: List[Tuple[float, float]]) -> bytes:
        """
        优化的差分编码，根据数据特点选择存储策略
        """
        if not coordinates:
            return b''

        if len(coordinates) == 1:
            # 单点情况，直接存储绝对坐标
            return struct.pack('dd', coordinates[0][0], coordinates[0][1])

        # 计算偏移量范围，决定使用哪种数据类型
        max_delta = 0
        deltas = []
        for i in range(1, len(coordinates)):
            dx = coordinates[i][0] - coordinates[i - 1][0]
            dy = coordinates[i][1] - coordinates[i - 1][1]
            deltas.append((dx, dy))
            max_delta = max(max_delta, abs(dx), abs(dy))

        # 存储第一个点的绝对坐标
        encoded_data = struct.pack('dd', coordinates[0][0], coordinates[0][1])

        # 根据偏移量范围选择合适的存储格式
        if max_delta < 32767:  # short类型范围
            # 使用short类型存储偏移量（2字节/坐标）
            encoded_data += b'\x00'  # 标记使用short类型
            for dx, dy in deltas:
                encoded_data += struct.pack('hh', int(dx), int(dy))
        elif max_delta < 2147483647:  # int类型范围
            # 使用int类型存储偏移量（4字节/坐标）
            encoded_data += b'\x01'  # 标记使用int类型
            for dx, dy in deltas:
                encoded_data += struct.pack('ii', int(dx), int(dy))
        else:
            # 使用float类型存储偏移量（4字节/坐标）
            encoded_data += b'\x02'  # 标记使用float类型
            for dx, dy in deltas:
                encoded_data += struct.pack('ff', dx, dy)

        return encoded_data

    def _decode_coordinates_delta_optimized(
            self, data: bytes) -> List[Tuple[float, float]]:
        """
        解码优化的差分编码坐标数据
        """
        if not data or len(data) < 16:
            return []

        coordinates = []

        # 读取第一个点的绝对坐标
        x, y = struct.unpack('dd', data[:16])
        coordinates.append((x, y))

        if len(data) <= 16:
            return coordinates

        # 读取数据类型标记
        type_flag = data[16]
        pos = 17

        if type_flag == 0:  # short类型
            while pos + 4 <= len(data):
                dx, dy = struct.unpack('hh', data[pos:pos + 4])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 4
        elif type_flag == 1:  # int类型
            while pos + 8 <= len(data):
                dx, dy = struct.unpack('ii', data[pos:pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8
        elif type_flag == 2:  # float类型
            while pos + 8 <= len(data):
                dx, dy = struct.unpack('ff', data[pos:pos + 8])
                prev_x, prev_y = coordinates[-1]
                coordinates.append((prev_x + dx, prev_y + dy))
                pos += 8

        return coordinates

    def convert(self, shapefile_path: str, s2_resolution: int = 15) -> str:
        """将Shapefile转换为自定义二进制格式"""
        print(f"开始转换Shapefile: {shapefile_path}")

        # 设置S2分辨率
        self.s2_adapter.resolution = s2_resolution

        # 打开Shapefile
        datasource = ogr.Open(shapefile_path)
        if not datasource:
            raise ValueError(f"无法打开Shapefile: {shapefile_path}")

        layer = datasource.GetLayer()
        if not layer:
            raise ValueError(f"无法获取图层")

        feature_count = layer.GetFeatureCount()
        print(f"共 {feature_count} 个要素")

        # 获取字段定义
        layer_defn = layer.GetLayerDefn()
        field_names = []
        field_types = []  # 存储字段类型信息
        for i in range(layer_defn.GetFieldCount()):
            field_defn = layer_defn.GetFieldDefn(i)
            field_names.append(field_defn.GetName())
            field_types.append(field_defn.GetType())

        # 创建字段信息字典并序列化存储
        field_info = {'names': field_names, 'types': field_types}

        # 将字段信息写入属性存储的开头
        field_info_bytes = pickle.dumps(field_info)
        with open(self.attribute_storage.attribute_file, 'wb') as f:
            # 先写入字段信息长度和字段信息
            f.write(struct.pack('I', len(field_info_bytes)))
            f.write(field_info_bytes)

        # 创建索引结构
        index_data = {
            'features': {},  # fid -> {geom_offset, attr_offset, s2_cells}
            's2_index': {}  # s2_cell_id -> [fid_list]
        }

        processed_count = 0
        for feature in layer:
            fid = feature.GetFID()
            geom = feature.GetGeometryRef()
            if not geom:
                continue

            # 处理几何数据
            coords = []
            geom_type = -1

            # 使用OGR几何类型常量替代数字
            geom_type_code = geom.GetGeometryType()

            # 点类型（包括2D和3D点，普通点和多点）
            if geom_type_code in (ogr.wkbPoint, ogr.wkbPoint25D,
                                  ogr.wkbMultiPoint, ogr.wkbMultiPoint25D):
                # 对于MultiPoint，需要提取所有点
                if geom_type_code in (ogr.wkbMultiPoint, ogr.wkbMultiPoint25D):
                    for i in range(geom.GetGeometryCount()):
                        sub_point = geom.GetGeometryRef(i)
                        coords.append((sub_point.GetX(), sub_point.GetY()))
                else:
                    coords = [(geom.GetX(), geom.GetY())]
                geom_type = 0

            # 线类型（包括LineString和MultiLineString）
            elif geom_type_code in (ogr.wkbLineString, ogr.wkbLineString25D,
                                    ogr.wkbMultiLineString,
                                    ogr.wkbMultiLineString25D):
                # 递归提取所有坐标点
                def extract_line_coordinates(geometry, coord_list):
                    if geometry.GetGeometryCount() > 0:
                        for i in range(geometry.GetGeometryCount()):
                            sub_geom = geometry.GetGeometryRef(i)
                            extract_line_coordinates(sub_geom, coord_list)
                    else:
                        for i in range(geometry.GetPointCount()):
                            coord_list.append(
                                (geometry.GetX(i), geometry.GetY(i)))

                extract_line_coordinates(geom, coords)
                geom_type = 1

            # 面类型（包括Polygon和MultiPolygon）
            elif geom_type_code in (ogr.wkbPolygon, ogr.wkbPolygon25D,
                                    ogr.wkbMultiPolygon,
                                    ogr.wkbMultiPolygon25D):
                # 递归提取所有坐标点
                def extract_polygon_coordinates(geometry, coord_list):
                    if geometry.GetGeometryCount() > 0:
                        for i in range(geometry.GetGeometryCount()):
                            sub_geom = geometry.GetGeometryRef(i)
                            extract_polygon_coordinates(sub_geom, coord_list)
                    else:
                        for i in range(geometry.GetPointCount()):
                            coord_list.append(
                                (geometry.GetX(i), geometry.GetY(i)))

                extract_polygon_coordinates(geom, coords)
                geom_type = 2

            else:
                print(f"跳过不支持的几何类型: {geom_type_code}")
                continue

            # 计算边界框
            bbox = self._calculate_bbox(coords)

            # 使用优化的差分编码压缩坐标数据
            coord_data = self._encode_coordinates_delta_optimized(coords)

            # 计算S2单元格
            s2_cell_ids = self.s2_adapter.calculate_s2_cells(bbox)

            # 创建几何数据对象
            geom_data = GeometryData(fid, geom_type, coord_data, bbox,
                                     s2_cell_ids)

            # 写入几何文件并记录偏移位置
            geom_offset = self.geometry_storage.write_geometry(geom_data)

            # 处理属性数据
            props = {}
            for field_name in field_names:
                try:
                    props[field_name] = feature.GetField(field_name)
                except:
                    props[field_name] = None

            attr_data = AttributeData(fid, props)
            # 使用pickle序列化属性数据以提高效率
            attr_bytes = pickle.dumps(props)

            # 追加写入属性文件
            attr_offset = 0
            with open(self.attribute_storage.attribute_file, 'ab') as f:
                attr_offset = f.tell()
                f.write(attr_bytes)

            # 更新索引
            index_data['features'][fid] = {
                'geom_offset': geom_offset,
                'attr_offset': attr_offset,
                's2_cells': s2_cell_ids
            }

            # 更新S2索引
            for cell_id in s2_cell_ids:
                if cell_id not in index_data['s2_index']:
                    index_data['s2_index'][cell_id] = []
                index_data['s2_index'][cell_id].append(fid)

            processed_count += 1
            if processed_count % 1000 == 0:
                print(f"已处理 {processed_count} 个要素")

        # 保存索引数据
        with open(self.index_file, 'wb') as f:
            pickle.dump(index_data, f)

        print(f"转换完成，共处理 {processed_count} 个要素")
        print(f"- 几何数据: {self.geometry_storage.geometry_file}")
        print(f"- 属性数据: {self.attribute_storage.attribute_file}")
        print(f"- 索引数据: {self.index_file}")

        return self.output_dir
