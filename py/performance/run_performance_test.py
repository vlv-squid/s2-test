# run_performance_test.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import sys
import os
import argparse
from pathlib import Path

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from performance_comparison import PerformanceComparison
from detailed_performance_analysis import DetailedPerformanceAnalysis


def run_basic_test(shapefile_path: str):
    """运行基础性能测试"""
    print("运行基础性能测试...")
    comparison = PerformanceComparison(shapefile_path)
    comparison.run_comprehensive_test()


def run_detailed_test(shapefile_path: str):
    """运行详细性能测试"""
    print("运行详细性能测试...")
    analysis = DetailedPerformanceAnalysis(shapefile_path)
    analysis.run_complete_analysis()


def run_quick_test(shapefile_path: str):
    """运行快速性能测试"""
    print("运行快速性能测试...")
    comparison = PerformanceComparison(shapefile_path)
    
    # 只测试小规模数据
    comparison.run_comprehensive_test(test_sizes=[100, 1000])


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='GIS存储格式性能测试工具')
    parser.add_argument('--shapefile', '-s', default='./data/test.shp',
                       help='Shapefile文件路径 (默认: ./data/test.shp)')
    parser.add_argument('--test-type', '-t', choices=['basic', 'detailed', 'quick'], 
                       default='basic', help='测试类型 (默认: basic)')
    parser.add_argument('--output-dir', '-o', default='./output_data',
                       help='输出目录 (默认: ./output_data)')
    
    args = parser.parse_args()
    
    # 检查文件是否存在
    if not os.path.exists(args.shapefile):
        print(f"错误: 找不到Shapefile文件 {args.shapefile}")
        print("请确保数据文件存在，或使用 --shapefile 参数指定正确的文件路径")
        return
    
    print("=" * 60)
    print("GIS存储格式性能测试")
    print("=" * 60)
    print(f"数据文件: {args.shapefile}")
    print(f"测试类型: {args.test_type}")
    print(f"输出目录: {args.output_dir}")
    print("=" * 60)
    
    try:
        if args.test_type == 'basic':
            run_basic_test(args.shapefile)
        elif args.test_type == 'detailed':
            run_detailed_test(args.shapefile)
        elif args.test_type == 'quick':
            run_quick_test(args.shapefile)
        
        print("\n测试完成！")
        
    except Exception as e:
        print(f"测试过程中发生错误: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
