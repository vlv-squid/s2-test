#
# created by:
#   @author: vlv-squid
#   @date: 2025-07-17
#

import os
import time
import sqlite3
import math
from collections import defaultdict
from osgeo import ogr, osr
import s2sphere
import numpy as np
import matplotlib.pyplot as plt
from rtree import index


class S2SpatialIndex:

    def __init__(self,
                 data_path,
                 index_db='./index_py/spatial_index.db',
                 s2_level=15):
        self.data_path = data_path
        self.index_db = index_db
        self.rtree_idx = None
        self.s2_level = s2_level
        self.feature_bounds = {}
        self.feature_count = 0

        # 生成 R 树索引文件路径（基于 SQLite 数据库路径）
        self.rtree_path = os.path.splitext(index_db)[0] + "_rtree"

        ogr.RegisterAll()

        if os.path.exists(self.index_db):
            print("索引数据库存在，加载要素信息...")
            datasource = ogr.Open(self.data_path)
            layer = datasource.GetLayer()
            self.feature_count = layer.GetFeatureCount()

            # 加载要素边界
            self.feature_bounds = {}
            for feature in layer:
                fid = feature.GetFID()
                geom = feature.GetGeometryRef()
                if geom:
                    env = geom.GetEnvelope()
                    self.feature_bounds[fid] = (env[0], env[2], env[1], env[3])

            # 尝试加载 R 树索引
            if self._rtree_index_exists():
                print("加载 R 树索引...")
                self.rtree_idx = index.Index(self.rtree_path)
                print("R 树索引加载成功")
            else:
                print("R 树索引文件不存在，请调用 build_index() 重建索引")
        else:
            print("索引数据库不存在，请调用 build_index() 构建索引")

    def _rtree_index_exists(self):
        """检查 R 树索引文件是否存在"""
        return (os.path.exists(self.rtree_path + '.dat')
                and os.path.exists(self.rtree_path + '.idx'))

    def build_index(self):
        """构建或重建空间索引"""
        # 删除旧的 R 树索引文件
        for ext in ['.dat', '.idx']:
            if os.path.exists(self.rtree_path + ext):
                os.remove(self.rtree_path + ext)

        start_time = time.time()

        # 处理 SQLite 数据库
        conn = None
        if not os.path.exists(self.index_db):
            # 创建新数据库
            conn = sqlite3.connect(self.index_db)
            cursor = conn.cursor()
            cursor.execute('''
                CREATE TABLE s2_index (
                    cell_id INTEGER,
                    fid INTEGER
                )
            ''')
            cursor.execute('CREATE INDEX idx_cell_id ON s2_index (cell_id)')
            print("创建新的索引数据库")
        else:
            # 清空现有数据库
            conn = sqlite3.connect(self.index_db)
            cursor = conn.cursor()
            cursor.execute("DELETE FROM s2_index")
            print("清空现有索引数据库")

        # 打开 GIS 数据源
        datasource = ogr.Open(self.data_path)
        layer = datasource.GetLayer()
        self.feature_count = layer.GetFeatureCount()
        print(f"开始构建索引，共 {self.feature_count} 个要素...")

        # 初始化 R 树索引（持久化到文件）
        rtree_properties = index.Property()
        rtree_properties.dimension = 2
        self.rtree_idx = index.Index(self.rtree_path,
                                     properties=rtree_properties)

        # 遍历所有要素
        index_data = []
        feature_bounds = []

        for feature in layer:
            fid = feature.GetFID()
            geom = feature.GetGeometryRef()
            if not geom:
                continue

            # 获取要素外包矩形
            env = geom.GetEnvelope()
            min_lon, max_lon, min_lat, max_lat = env
            bounds = (min_lon, min_lat, max_lon, max_lat)
            self.feature_bounds[fid] = bounds
            feature_bounds.append((fid, bounds))

            # 构建 S2 单元
            p1 = s2sphere.LatLng.from_degrees(min_lat, min_lon)
            p2 = s2sphere.LatLng.from_degrees(max_lat, max_lon)
            rect = s2sphere.LatLngRect.from_point_pair(p1, p2)

            coverer = s2sphere.RegionCoverer()
            coverer.min_level = self.s2_level
            coverer.max_level = self.s2_level
            coverer.max_cells = 8

            cell_ids = coverer.get_covering(rect)
            for cell_id in cell_ids:
                index_data.append((cell_id.id(), fid))

        # 批量插入 S2 索引
        cursor.executemany("INSERT INTO s2_index VALUES (?, ?)", index_data)
        conn.commit()
        conn.close()

        # 构建 R 树索引
        for fid, bounds in feature_bounds:
            min_lon, min_lat, max_lon, max_lat = bounds
            self.rtree_idx.insert(fid, (min_lon, min_lat, max_lon, max_lat))
        print(f"R树插入要素数: {len(feature_bounds)}")

        print(f"索引构建完成! 耗时: {time.time() - start_time:.2f}秒")
        print(f"S2索引条目: {len(index_data)}, R树条目: {len(feature_bounds)}")
        print(f"R树索引已保存到: {self.rtree_path}.*")

    def calculate_auto_level(self, bbox):
        """
        根据BBox面积自动计算S2层级
        :param bbox: (min_lon, min_lat, max_lon, max_lat)
        """
        min_lon, min_lat, max_lon, max_lat = bbox
        area = (max_lon - min_lon) * (max_lat - min_lat)
        return min(30, max(10, 30 - round(math.log10(area * 10000))))

    def query_by_bbox(self, bbox, use_rtree=True, exact_check=True):
        """
        通过BBox查询要素
        :param bbox: (min_lon, min_lat, max_lon, max_lat)
        :param use_rtree: 是否使用R树优化
        :param exact_check: 是否执行精确几何检查
        :return: 匹配的要素ID列表
        """
        min_lon, min_lat, max_lon, max_lat = bbox
        start_time = time.time()

        # 自动计算最佳S2层级
        auto_level = self.calculate_auto_level(bbox)
        use_level = min(self.s2_level, auto_level)

        # 计算查询区域的S2单元
        p1 = s2sphere.LatLng.from_degrees(min_lat, min_lon)
        p2 = s2sphere.LatLng.from_degrees(max_lat, max_lon)
        query_rect = s2sphere.LatLngRect.from_point_pair(p1, p2)

        coverer = s2sphere.RegionCoverer()
        coverer.min_level = use_level
        coverer.max_level = use_level
        query_cells = coverer.get_covering(query_rect)

        # 从数据库查询候选要素
        conn = sqlite3.connect(self.index_db)
        cursor = conn.cursor()

        placeholders = ','.join(['?'] * len(query_cells))
        cell_ids = [cell.id() for cell in query_cells]

        cursor.execute(
            f"""
            SELECT DISTINCT fid 
            FROM s2_index 
            WHERE cell_id IN ({placeholders})
        """, cell_ids)

        candidate_fids = [row[0] for row in cursor.fetchall()]
        conn.close()

        # 使用RTree进行快速过滤
        if use_rtree and self.rtree_idx:
            filtered_fids = list(self.rtree_idx.intersection(bbox))
            candidate_fids = list(set(candidate_fids) & set(filtered_fids))

        # 精确几何验证（仅用外包矩形相交）
        results = []
        if exact_check:
            for fid in candidate_fids:
                f_minx, f_miny, f_maxx, f_maxy = self.feature_bounds[fid]
                if self._intersect_rect(f_minx, f_miny, f_maxx, f_maxy, *bbox):
                    results.append(fid)
        else:
            results = candidate_fids

        print(f"查询完成! 耗时: {(time.time() - start_time)*1000:.2f}ms")
        print(f"候选要素: {len(candidate_fids)}, 结果要素: {len(results)}")
        return results

    def _intersect_rect(self, a_minx, a_miny, a_maxx, a_maxy, b_minx, b_miny,
                        b_maxx, b_maxy):
        """判断两个矩形是否相交"""
        return not (a_maxx < b_minx or a_minx > b_maxx or a_maxy < b_miny
                    or a_miny > b_maxy)


def run_performance_test(data_path, bbox):
    """运行性能测试"""
    print("\n===== 性能测试开始 =====")

    # 测试1: 纯GDAL顺序扫描
    print("\n[测试1] 纯GDAL顺序扫描:")
    start_time = time.time()
    datasource = ogr.Open(data_path)
    results = []
    layer = datasource.GetLayer()
    min_lon, min_lat, max_lon, max_lat = bbox
    layer.SetSpatialFilterRect(min_lon, min_lat, max_lon, max_lat)
    results.extend([feat.GetFID() for feat in layer])
    print(f"耗时: {(time.time() - start_time)*1000:.2f}ms, 结果数: {len(results)}")
    query_name = "纯GDAL顺序扫描索引"
    visualize_results(data_path, results, sample_bbox, query_name)

    # 测试2: 纯S2索引 (无精确验证)
    print("\n[测试2] 纯S2索引 (无精确验证):")
    start_time = time.time()
    results = indexer.query_by_bbox(bbox, use_rtree=False, exact_check=False)
    print(f"总耗时: {(time.time() - start_time)*1000:.2f}ms")
    query_name = "纯S2索引"
    visualize_results(data_path, results, sample_bbox, query_name)

    # 测试3: S2索引 + 矩形精确验证
    print("\n[测试3] S2 + 矩形精确验证:")
    start_time = time.time()
    results = indexer.query_by_bbox(bbox, use_rtree=False, exact_check=True)
    print(f"总耗时: {(time.time() - start_time)*1000:.2f}ms")
    query_name = "S2 + 矩形索引"
    visualize_results(data_path, results, sample_bbox, query_name)

    # 测试4: S2索引 + R树验证
    print("\n[测试4] S2 + R树验证:")
    start_time = time.time()
    results = indexer.query_by_bbox(bbox, use_rtree=True, exact_check=False)
    print(f"总耗时: {(time.time() - start_time)*1000:.2f}ms")
    query_name = "S2 + R树索引"
    visualize_results(data_path, results, sample_bbox, query_name)

    # 测试5: S2索引 + R树优化 + 矩形精确验证
    print("\n[测试5] S2 + R树 + 矩形精确验证:")
    start_time = time.time()
    results = indexer.query_by_bbox(bbox, use_rtree=True, exact_check=True)
    print(f"总耗时: {(time.time() - start_time)*1000:.2f}ms")
    query_name = "S2 + R树 + 矩形索引"
    visualize_results(data_path, results, sample_bbox, query_name)

    print("===== 性能测试结束 =====")


def visualize_results(data_path, results, bbox, query_name):
    """可视化查询结果"""
    datasource = ogr.Open(data_path)

    fig, ax = plt.subplots(figsize=(12, 8))

    # 绘制BBox
    min_lon, min_lat, max_lon, max_lat = bbox
    rect = plt.Rectangle((min_lon, min_lat),
                         max_lon - min_lon,
                         max_lat - min_lat,
                         fill=False,
                         color='red',
                         linewidth=2)
    ax.add_patch(rect)

    colors = ['blue', 'green', 'purple']
    layer = datasource.GetLayer()
    for feature in layer:
        geom = feature.GetGeometryRef()
        if geom is None:
            continue
        if geom.GetGeometryType() == ogr.wkbPoint:
            x, y = geom.GetX(), geom.GetY()
            ax.plot(x, y, 'o', color='gray', markersize=2, alpha=0.3)
        elif geom.GetGeometryType() in [
                ogr.wkbLineString, ogr.wkbMultiLineString
        ]:
            coords = [(g.GetX(), g.GetY()) for g in geom]
            xs, ys = zip(*coords) if coords else ([], [])
            ax.plot(xs, ys, color='gray', linewidth=0.5, alpha=0.3)
        elif geom.GetGeometryType() in [ogr.wkbPolygon, ogr.wkbMultiPolygon]:
            ring = geom.GetGeometryRef(
                0) if geom.GetGeometryCount() > 0 else None
            if ring:
                coords = [(ring.GetX(j), ring.GetY(j))
                          for j in range(ring.GetPointCount())]
                xs, ys = zip(*coords)
                ax.fill(xs, ys, color='lightgray', alpha=0.1)

    # 高亮显示结果要素
    for fid in results:
        feature = layer.GetFeature(fid)
        geom = feature.GetGeometryRef()
        if geom is None:
            continue
        if geom.GetGeometryType() == ogr.wkbPoint:
            ax.plot(geom.GetX(), geom.GetY(), 'ro', markersize=6)
        elif geom.GetGeometryType() in [
                ogr.wkbLineString, ogr.wkbMultiLineString
        ]:
            coords = [(g.GetX(), g.GetY()) for g in geom]
            xs, ys = zip(*coords) if coords else ([], [])
            ax.plot(xs, ys, 'r-', linewidth=2)
        elif geom.GetGeometryType() in [ogr.wkbPolygon, ogr.wkbMultiPolygon]:
            ring = geom.GetGeometryRef(0)
            coords = [(ring.GetX(j), ring.GetY(j))
                      for j in range(ring.GetPointCount())]
            xs, ys = zip(*coords)
            ax.fill(xs, ys, color='red', alpha=0.4)

    ax.set_title(f"Spatial Query Results ({len(results)} features)")
    ax.set_xlabel('Longitude')
    ax.set_ylabel('Latitude')
    ax.grid(True)
    plt.tight_layout()
    outpath = os.path.join("./png", query_name + ".png")
    plt.savefig(outpath)
    print("可视化结果已保存为" + query_name + ".png")


if __name__ == "__main__":
    # 指定实际的数据路径
    test_data = "./data/test.shp"
    # 定义查询范围（根据实际需求定义）
    sample_bbox = (103.2504, 26.4297, 103.3028, 26.4747)

    # 初始化空间索引系统
    indexer = S2SpatialIndex(test_data, s2_level=15)
    # 构建索引（如果索引不存在会自动创建）
    indexer.build_index()

    # 性能测试
    run_performance_test(test_data, sample_bbox)
