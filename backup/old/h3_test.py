#
# created by:
#   @author: vlv-squid
#   @date: 2025-07-22
#

import geopandas as gpd
from shapely.geometry import box
from h3 import geo_to_cells


def polygon_to_h3_indexes(geom, resolution):
    """使用 geo_to_cells 替代 polyfill_geojson"""
    return geo_to_cells(geom, resolution)


def bbox_to_h3_indexes(minx, miny, maxx, maxy, resolution):
    """将 BBox 转换为 H3 索引集合"""
    from shapely.geometry import box
    bbox_geom = box(minx, miny, maxx, maxy)
    return geo_to_cells(bbox_geom, resolution)


def build_h3_index(gdf, resolution):
    """构建 H3 索引到要素ID的倒排索引"""
    h3_to_feature = {}
    for idx, row in gdf.iterrows():
        # h3_indexes = polygon_to_h3_indexes(row.geometry, resolution)
        h3_indexes = polygon_to_h3_indexes(row.geometry.buffer(0.001),
                                           resolution)
        for h in h3_indexes:
            if h not in h3_to_feature:
                h3_to_feature[h] = set()
            h3_to_feature[h].add(idx)
    return h3_to_feature


def query_by_bbox(gdf, h3_index_map, bbox, resolution):
    """通过 bbox 查询命中的要素索引集"""
    h3_cover = bbox_to_h3_indexes(*bbox, resolution)
    candidate_ids = set()
    for h in h3_cover:
        if h in h3_index_map:
            candidate_ids |= h3_index_map[h]
    return gdf.loc[list(candidate_ids)]


def precise_filter(gdf, bbox_geom):
    """用精确 geometry 判断是否相交"""
    return gdf[gdf.intersects(bbox_geom)]


if __name__ == "__main__":
    # === 设置参数 ===
    shapefile_path = "/home/chenming/Data/GIS_DATA/shapefile/dltb_532300_2020.shp"  # 请修改为实际路径
    h3_resolution = 10

    # 示例查询 BBox（经纬度）：云南某地区
    query_bbox = (103.2504, 26.4297, 103.3028, 26.4747)
    bbox_geom = box(*query_bbox)

    # === 加载 shapefile ===
    print("Loading shapefile...")
    gdf = gpd.read_file(shapefile_path)

    # === 构建 H3 索引 ===
    print("Building H3 spatial index...")
    h3_index_map = build_h3_index(gdf, h3_resolution)

    # === 初步筛选（H3 索引） ===
    print("Querying by H3 index...")
    coarse_result = query_by_bbox(gdf, h3_index_map, query_bbox, h3_resolution)
    print(f"Candidates from H3: {len(coarse_result)}")

    # === 精确过滤 ===
    print("Filtering geometries...")
    final_result = precise_filter(coarse_result, bbox_geom)
    print(f"Final result count: {len(final_result)}")

    # === 输出 ===
    final_result.to_file("query_result.shp")  # 保存结果为 shapefile
    print("Filtered result saved to 'query_result.shp'.")
