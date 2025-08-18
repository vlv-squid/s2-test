# memory_analyzer.py
# 内存使用分析工具

import os
import struct
import json
import psutil
import time
from typing import Dict, List, Tuple
from dataclasses import dataclass


@dataclass
class MemoryUsage:
    """内存使用统计"""
    total_elements: int
    offset_index_memory: int  # 字节
    geometry_file_size: int   # 字节
    attribute_file_size: int  # 字节
    estimated_total_memory: int  # 字节


class MemoryAnalyzer:
    """内存使用分析器"""
    
    def __init__(self, geometry_file: str, attribute_file: str):
        self.geometry_file = geometry_file
        self.attribute_file = attribute_file
    
    def analyze_current_memory(self) -> MemoryUsage:
        """分析当前文件的内存使用情况"""
        if not os.path.exists(self.geometry_file):
            return MemoryUsage(0, 0, 0, 0, 0)
        
        # 统计要素数量
        total_elements = self._count_elements()
        
        # 计算偏移索引内存
        offset_index_memory = total_elements * 16  # 8字节ID + 8字节偏移量
        
        # 文件大小
        geometry_file_size = os.path.getsize(self.geometry_file) if os.path.exists(self.geometry_file) else 0
        attribute_file_size = os.path.getsize(self.attribute_file) if os.path.exists(self.attribute_file) else 0
        
        # 估算总内存使用（包括Python对象开销）
        estimated_total_memory = offset_index_memory * 2  # Python字典开销约2倍
        
        return MemoryUsage(
            total_elements=total_elements,
            offset_index_memory=offset_index_memory,
            geometry_file_size=geometry_file_size,
            attribute_file_size=attribute_file_size,
            estimated_total_memory=estimated_total_memory
        )
    
    def _count_elements(self) -> int:
        """统计要素数量"""
        count = 0
        if not os.path.exists(self.geometry_file):
            return count
            
        with open(self.geometry_file, 'rb') as f:
            while True:
                # 读取feature_id
                fid_data = f.read(8)
                if len(fid_data) < 8:
                    break
                
                count += 1
                
                # 跳过记录结构
                try:
                    f.seek(40, 1)  # 跳过geometry_type + bbox
                    coord_size_data = f.read(4)
                    if len(coord_size_data) < 4:
                        break
                    coord_size = struct.unpack('I', coord_size_data)[0]
                    f.seek(coord_size, 1)  # 跳过坐标数据
                except:
                    break
        
        return count
    
    def estimate_million_scale_memory(self, target_elements: int = 10_000_000) -> Dict:
        """估算千万级数据的内存使用"""
        current_usage = self.analyze_current_memory()
        
        if current_usage.total_elements == 0:
            return {
                "error": "无法分析，当前没有数据"
            }
        
        # 计算平均每个要素的大小
        avg_geometry_size = current_usage.geometry_file_size / current_usage.total_elements
        avg_attribute_size = current_usage.attribute_file_size / current_usage.total_elements
        
        # 估算千万级数据
        estimated_geometry_size = avg_geometry_size * target_elements
        estimated_attribute_size = avg_attribute_size * target_elements
        estimated_offset_index_memory = target_elements * 16 * 2  # 包含Python开销
        
        total_estimated_memory = estimated_offset_index_memory + estimated_geometry_size + estimated_attribute_size
        
        return {
            "current_elements": current_usage.total_elements,
            "target_elements": target_elements,
            "current_avg_geometry_size": avg_geometry_size,
            "current_avg_attribute_size": avg_attribute_size,
            "estimated_geometry_file_size_mb": estimated_geometry_size / (1024 * 1024),
            "estimated_attribute_file_size_mb": estimated_attribute_size / (1024 * 1024),
            "estimated_offset_index_memory_mb": estimated_offset_index_memory / (1024 * 1024),
            "total_estimated_memory_gb": total_estimated_memory / (1024 * 1024 * 1024),
            "memory_pressure_level": self._assess_memory_pressure(total_estimated_memory)
        }
    
    def _assess_memory_pressure(self, estimated_memory: int) -> str:
        """评估内存压力等级"""
        memory_gb = estimated_memory / (1024 * 1024 * 1024)
        
        if memory_gb < 1:
            return "低压力"
        elif memory_gb < 4:
            return "中等压力"
        elif memory_gb < 8:
            return "高压力"
        else:
            return "极高压力"
    
    def get_system_memory_info(self) -> Dict:
        """获取系统内存信息"""
        memory = psutil.virtual_memory()
        return {
            "total_memory_gb": memory.total / (1024 * 1024 * 1024),
            "available_memory_gb": memory.available / (1024 * 1024 * 1024),
            "used_memory_gb": memory.used / (1024 * 1024 * 1024),
            "memory_percent": memory.percent
        }


def print_memory_analysis(geometry_file: str, attribute_file: str):
    """打印内存分析结果"""
    analyzer = MemoryAnalyzer(geometry_file, attribute_file)
    
    print("=== 内存使用分析 ===")
    
    # 当前内存使用
    current_usage = analyzer.analyze_current_memory()
    print(f"当前要素数量: {current_usage.total_elements:,}")
    print(f"偏移索引内存: {current_usage.offset_index_memory / (1024*1024):.2f} MB")
    print(f"几何文件大小: {current_usage.geometry_file_size / (1024*1024):.2f} MB")
    print(f"属性文件大小: {current_usage.attribute_file_size / (1024*1024):.2f} MB")
    
    # 系统内存信息
    system_memory = analyzer.get_system_memory_info()
    print(f"\n系统内存信息:")
    print(f"总内存: {system_memory['total_memory_gb']:.1f} GB")
    print(f"可用内存: {system_memory['available_memory_gb']:.1f} GB")
    print(f"已用内存: {system_memory['used_memory_gb']:.1f} GB ({system_memory['memory_percent']:.1f}%)")
    
    # 千万级估算
    print(f"\n=== 千万级数据估算 ===")
    million_scale_estimate = analyzer.estimate_million_scale_memory()
    
    if "error" in million_scale_estimate:
        print(f"错误: {million_scale_estimate['error']}")
        return
    
    print(f"目标要素数量: {million_scale_estimate['target_elements']:,}")
    print(f"平均几何大小: {million_scale_estimate['current_avg_geometry_size']:.1f} 字节")
    print(f"平均属性大小: {million_scale_estimate['current_avg_attribute_size']:.1f} 字节")
    print(f"估算几何文件大小: {million_scale_estimate['estimated_geometry_file_size_mb']:.1f} MB")
    print(f"估算属性文件大小: {million_scale_estimate['estimated_attribute_file_size_mb']:.1f} MB")
    print(f"估算偏移索引内存: {million_scale_estimate['estimated_offset_index_memory_mb']:.1f} MB")
    print(f"估算总内存使用: {million_scale_estimate['total_estimated_memory_gb']:.1f} GB")
    print(f"内存压力等级: {million_scale_estimate['memory_pressure_level']}")
    
    # 内存压力评估
    available_gb = system_memory['available_memory_gb']
    estimated_gb = million_scale_estimate['total_estimated_memory_gb']
    
    if estimated_gb > available_gb:
        print(f"\n⚠️  警告: 估算内存使用 ({estimated_gb:.1f} GB) 超过可用内存 ({available_gb:.1f} GB)")
        print("建议优化策略:")
        print("1. 使用分块索引而不是全内存索引")
        print("2. 实现流式读取机制")
        print("3. 使用数据库存储替代文件存储")
    else:
        print(f"\n✅ 内存使用估算 ({estimated_gb:.1f} GB) 在可用内存范围内 ({available_gb:.1f} GB)")


if __name__ == "__main__":
    # 分析当前测试数据
    geometry_file = "./output_data/test_geom.dat"
    attribute_file = "./output_data/test_attr.dat"
    
    print_memory_analysis(geometry_file, attribute_file)
