# detailed_performance_analysis.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-15

import sys
import os
import time
import json
import statistics
from pathlib import Path
from typing import List, Tuple, Dict, Any
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from performance_comparison import PerformanceComparison


class DetailedPerformanceAnalysis:
    """详细性能分析类"""

    def __init__(self, shapefile_path: str, output_dir: str = "./output_data"):
        self.shapefile_path = shapefile_path
        self.output_dir = output_dir
        self.results_dir = f"{output_dir}/performance_results"
        os.makedirs(self.results_dir, exist_ok=True)

        # 初始化性能对比测试对象
        self.comparison = PerformanceComparison(shapefile_path, output_dir)

        # 测试配置
        self.test_sizes = [100, 500, 1000, 5000, 10000]
        self.test_iterations = 5  # 每个测试重复次数
        self.test_bboxes = [
            (103.2504, 26.4297, 103.3028, 26.4747),  # 小范围
            (103.0, 26.0, 104.0, 27.0),  # 中等范围
            (102.0, 25.0, 105.0, 28.0),  # 大范围
        ]

    def run_multiple_tests(self, test_func, *args, **kwargs) -> List[Dict]:
        """运行多次测试并返回结果列表"""
        results = []
        for i in range(self.test_iterations):
            print(f"  测试 {i+1}/{self.test_iterations}...")
            try:
                result = test_func(*args, **kwargs)
                if result:
                    results.append(result)
            except Exception as e:
                print(f"    测试失败: {e}")
        return results

    def calculate_statistics(self, results: List[Dict], metric: str) -> Dict:
        """计算统计信息"""
        if not results:
            return {}

        values = [r.get(metric, 0) for r in results if r.get(metric, 0) > 0]
        if not values:
            return {}

        return {
            "count": len(values),
            "mean": statistics.mean(values),
            "median": statistics.median(values),
            "std": statistics.stdev(values) if len(values) > 1 else 0,
            "min": min(values),
            "max": max(values),
            "values": values,
        }

    def test_sequential_read_performance(self) -> Dict:
        """测试顺序读取性能"""
        print("\n" + "=" * 50)
        print("顺序读取性能测试")
        print("=" * 50)

        results = {"ogr": {}, "custom": {}, "comparison": {}}

        for size in self.test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(
                self.comparison.test_ogr_sequential_read, size
            )

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.comparison.test_custom_sequential_read, size
            )

            # 计算统计信息
            ogr_stats = {
                "geom_rate": self.calculate_statistics(ogr_results, "geom_rate"),
                "attr_rate": self.calculate_statistics(ogr_results, "attr_rate"),
                "total_rate": self.calculate_statistics(ogr_results, "total_rate"),
            }

            custom_stats = {
                "geom_rate": self.calculate_statistics(custom_results, "geom_rate"),
                "attr_rate": self.calculate_statistics(custom_results, "attr_rate"),
                "total_rate": self.calculate_statistics(custom_results, "total_rate"),
            }

            results["ogr"][size] = ogr_stats
            results["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats["total_rate"] and custom_stats["total_rate"]:
                improvement = (
                    custom_stats["total_rate"]["mean"] / ogr_stats["total_rate"]["mean"]
                    - 1
                ) * 100
                results["comparison"][size] = improvement

                print(f"    性能提升: {improvement:+.1f}%")

        return results

    def test_random_read_performance(self) -> Dict:
        """测试随机读取性能"""
        print("\n" + "=" * 50)
        print("随机读取性能测试")
        print("=" * 50)

        results = {"ogr": {}, "custom": {}, "comparison": {}}

        for size in self.test_sizes:
            print(f"\n测试规模: {size}个要素")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(
                self.comparison.test_ogr_random_read, size
            )

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.comparison.test_custom_random_read, size
            )

            # 计算统计信息
            ogr_stats = {
                "geom_rate": self.calculate_statistics(ogr_results, "geom_rate"),
                "attr_rate": self.calculate_statistics(ogr_results, "attr_rate"),
                "total_rate": self.calculate_statistics(ogr_results, "total_rate"),
            }

            custom_stats = {
                "geom_rate": self.calculate_statistics(custom_results, "geom_rate"),
                "attr_rate": self.calculate_statistics(custom_results, "attr_rate"),
                "total_rate": self.calculate_statistics(custom_results, "total_rate"),
            }

            results["ogr"][size] = ogr_stats
            results["custom"][size] = custom_stats

            # 计算性能提升
            if ogr_stats["total_rate"] and custom_stats["total_rate"]:
                improvement = (
                    custom_stats["total_rate"]["mean"] / ogr_stats["total_rate"]["mean"]
                    - 1
                ) * 100
                results["comparison"][size] = improvement

                print(f"    性能提升: {improvement:+.1f}%")

        return results

    def test_spatial_query_performance(self) -> Dict:
        """测试空间查询性能"""
        print("\n" + "=" * 50)
        print("空间查询性能测试")
        print("=" * 50)

        results = {"ogr": {}, "custom": {}, "comparison": {}}

        for i, bbox in enumerate(self.comparison.test_bboxes):
            print(f"\n查询范围 {i+1}: {bbox}")

            # OGR测试
            print("  OGR测试...")
            ogr_results = self.run_multiple_tests(
                self.comparison.test_ogr_spatial_query, bbox
            )

            # 自定义格式测试
            print("  自定义格式测试...")
            custom_results = self.run_multiple_tests(
                self.comparison.test_custom_spatial_query, bbox
            )

            # 计算统计信息
            ogr_stats = {
                "query_time": self.calculate_statistics(ogr_results, "query_time"),
                "query_rate": self.calculate_statistics(ogr_results, "query_rate"),
                "result_count": self.calculate_statistics(ogr_results, "result_count"),
            }

            custom_stats = {
                "query_time": self.calculate_statistics(custom_results, "query_time"),
                "query_rate": self.calculate_statistics(custom_results, "query_rate"),
                "result_count": self.calculate_statistics(
                    custom_results, "result_count"
                ),
                "candidate_count": self.calculate_statistics(
                    custom_results, "candidate_count"
                ),
            }

            results["ogr"][f"bbox_{i+1}"] = ogr_stats
            results["custom"][f"bbox_{i+1}"] = custom_stats

            # 计算性能提升
            if ogr_stats["query_time"] and custom_stats["query_time"]:
                improvement = (
                    ogr_stats["query_time"]["mean"] / custom_stats["query_time"]["mean"]
                    - 1
                ) * 100
                results["comparison"][f"bbox_{i+1}"] = improvement

                print(f"    查询速度提升: {improvement:+.1f}%")

        return results

    def generate_performance_charts(
        self, sequential_results: Dict, random_results: Dict, spatial_results: Dict
    ):
        """生成性能对比图表"""
        print("\n" + "=" * 50)
        print("生成性能对比图表")
        print("=" * 50)

        # 设置中文字体
        plt.rcParams["font.sans-serif"] = ["SimHei", "DejaVu Sans"]
        plt.rcParams["axes.unicode_minus"] = False

        # 1. 顺序读取性能对比
        fig, axes = plt.subplots(2, 2, figsize=(15, 12))
        fig.suptitle("GIS存储格式性能对比分析", fontsize=16)

        # 顺序读取 - 总速率
        ax1 = axes[0, 0]
        sizes = list(sequential_results["ogr"].keys())
        ogr_rates = [sequential_results["ogr"][s]["total_rate"]["mean"] for s in sizes]
        custom_rates = [
            sequential_results["custom"][s]["total_rate"]["mean"] for s in sizes
        ]

        x = np.arange(len(sizes))
        width = 0.35

        ax1.bar(x - width / 2, ogr_rates, width, label="OGR", alpha=0.8)
        ax1.bar(x + width / 2, custom_rates, width, label="自定义格式", alpha=0.8)
        ax1.set_xlabel("要素数量")
        ax1.set_ylabel("读取速率 (要素/秒)")
        ax1.set_title("顺序读取性能对比")
        ax1.set_xticks(x)
        ax1.set_xticklabels(sizes)
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # 随机读取 - 总速率
        ax2 = axes[0, 1]
        ogr_rates = [random_results["ogr"][s]["total_rate"]["mean"] for s in sizes]
        custom_rates = [
            random_results["custom"][s]["total_rate"]["mean"] for s in sizes
        ]

        ax2.bar(x - width / 2, ogr_rates, width, label="OGR", alpha=0.8)
        ax2.bar(x + width / 2, custom_rates, width, label="自定义格式", alpha=0.8)
        ax2.set_xlabel("要素数量")
        ax2.set_ylabel("读取速率 (要素/秒)")
        ax2.set_title("随机读取性能对比")
        ax2.set_xticks(x)
        ax2.set_xticklabels(sizes)
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        # 性能提升对比
        ax3 = axes[1, 0]
        sequential_improvements = [
            sequential_results["comparison"].get(s, 0) for s in sizes
        ]
        random_improvements = [random_results["comparison"].get(s, 0) for s in sizes]

        ax3.plot(
            sizes,
            sequential_improvements,
            "o-",
            label="顺序读取",
            linewidth=2,
            markersize=8,
        )
        ax3.plot(
            sizes,
            random_improvements,
            "s-",
            label="随机读取",
            linewidth=2,
            markersize=8,
        )
        ax3.set_xlabel("要素数量")
        ax3.set_ylabel("性能提升 (%)")
        ax3.set_title("性能提升对比")
        ax3.legend()
        ax3.grid(True, alpha=0.3)
        ax3.axhline(y=0, color="black", linestyle="--", alpha=0.5)

        # 空间查询性能
        ax4 = axes[1, 1]
        bbox_names = list(spatial_results["ogr"].keys())
        ogr_times = [
            spatial_results["ogr"][b]["query_time"]["mean"] * 1000 for b in bbox_names
        ]
        custom_times = [
            spatial_results["custom"][b]["query_time"]["mean"] * 1000
            for b in bbox_names
        ]

        x_bbox = np.arange(len(bbox_names))
        ax4.bar(x_bbox - width / 2, ogr_times, width, label="OGR", alpha=0.8)
        ax4.bar(x_bbox + width / 2, custom_times, width, label="自定义格式", alpha=0.8)
        ax4.set_xlabel("查询范围")
        ax4.set_ylabel("查询时间 (毫秒)")
        ax4.set_title("空间查询性能对比")
        ax4.set_xticks(x_bbox)
        ax4.set_xticklabels(["小范围", "中等范围", "大范围"])
        ax4.legend()
        ax4.grid(True, alpha=0.3)

        plt.tight_layout()
        chart_path = f"{self.results_dir}/performance_comparison.png"
        plt.savefig(chart_path, dpi=300, bbox_inches="tight")
        print(f"性能对比图表已保存: {chart_path}")

        # 2. 详细统计图表
        fig2, axes2 = plt.subplots(2, 2, figsize=(15, 12))
        fig2.suptitle("详细性能统计", fontsize=16)

        # 几何数据读取性能
        ax1 = axes2[0, 0]
        ogr_geom_rates = [
            sequential_results["ogr"][s]["geom_rate"]["mean"] for s in sizes
        ]
        custom_geom_rates = [
            sequential_results["custom"][s]["geom_rate"]["mean"] for s in sizes
        ]

        ax1.bar(x - width / 2, ogr_geom_rates, width, label="OGR", alpha=0.8)
        ax1.bar(x + width / 2, custom_geom_rates, width, label="自定义格式", alpha=0.8)
        ax1.set_xlabel("要素数量")
        ax1.set_ylabel("几何读取速率 (要素/秒)")
        ax1.set_title("几何数据读取性能")
        ax1.set_xticks(x)
        ax1.set_xticklabels(sizes)
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # 属性数据读取性能
        ax2 = axes2[0, 1]
        ogr_attr_rates = [
            sequential_results["ogr"][s]["attr_rate"]["mean"] for s in sizes
        ]
        custom_attr_rates = [
            sequential_results["custom"][s]["attr_rate"]["mean"] for s in sizes
        ]

        ax2.bar(x - width / 2, ogr_attr_rates, width, label="OGR", alpha=0.8)
        ax2.bar(x + width / 2, custom_attr_rates, width, label="自定义格式", alpha=0.8)
        ax2.set_xlabel("要素数量")
        ax2.set_ylabel("属性读取速率 (要素/秒)")
        ax2.set_title("属性数据读取性能")
        ax2.set_xticks(x)
        ax2.set_xticklabels(sizes)
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        # 查询结果数量对比
        ax3 = axes2[1, 0]
        ogr_counts = [
            spatial_results["ogr"][b]["result_count"]["mean"] for b in bbox_names
        ]
        custom_counts = [
            spatial_results["custom"][b]["result_count"]["mean"] for b in bbox_names
        ]

        ax3.bar(x_bbox - width / 2, ogr_counts, width, label="OGR", alpha=0.8)
        ax3.bar(x_bbox + width / 2, custom_counts, width, label="自定义格式", alpha=0.8)
        ax3.set_xlabel("查询范围")
        ax3.set_ylabel("结果要素数量")
        ax3.set_title("查询结果数量对比")
        ax3.set_xticks(x_bbox)
        ax3.set_xticklabels(["小范围", "中等范围", "大范围"])
        ax3.legend()
        ax3.grid(True, alpha=0.3)

        # 查询效率对比
        ax4 = axes2[1, 1]
        ogr_efficiency = [
            ogr_counts[i] / (ogr_times[i] / 1000) if ogr_times[i] > 0 else 0
            for i in range(len(ogr_counts))
        ]
        custom_efficiency = [
            custom_counts[i] / (custom_times[i] / 1000) if custom_times[i] > 0 else 0
            for i in range(len(custom_counts))
        ]

        ax4.bar(x_bbox - width / 2, ogr_efficiency, width, label="OGR", alpha=0.8)
        ax4.bar(
            x_bbox + width / 2, custom_efficiency, width, label="自定义格式", alpha=0.8
        )
        ax4.set_xlabel("查询范围")
        ax4.set_ylabel("查询效率 (要素/秒)")
        ax4.set_title("查询效率对比")
        ax4.set_xticks(x_bbox)
        ax4.set_xticklabels(["小范围", "中等范围", "大范围"])
        ax4.legend()
        ax4.grid(True, alpha=0.3)

        plt.tight_layout()
        detail_chart_path = f"{self.results_dir}/detailed_statistics.png"
        plt.savefig(detail_chart_path, dpi=300, bbox_inches="tight")
        print(f"详细统计图表已保存: {detail_chart_path}")

        plt.show()

    def save_results(
        self, sequential_results: Dict, random_results: Dict, spatial_results: Dict
    ):
        """保存测试结果到JSON文件"""
        all_results = {
            "test_config": {
                "test_sizes": self.test_sizes,
                "test_iterations": self.test_iterations,
                "test_bboxes": self.test_bboxes,
            },
            "sequential_read": sequential_results,
            "random_read": random_results,
            "spatial_query": spatial_results,
            "summary": self.generate_summary(
                sequential_results, random_results, spatial_results
            ),
        }

        results_path = f"{self.results_dir}/performance_results.json"
        with open(results_path, "w", encoding="utf-8") as f:
            json.dump(all_results, f, indent=2, ensure_ascii=False)

        print(f"测试结果已保存: {results_path}")

    def generate_summary(
        self, sequential_results: Dict, random_results: Dict, spatial_results: Dict
    ) -> Dict:
        """生成测试总结"""
        summary = {"overall_performance": {}, "key_findings": [], "recommendations": []}

        # 计算整体性能提升
        sequential_improvements = list(sequential_results["comparison"].values())
        random_improvements = list(random_results["comparison"].values())
        spatial_improvements = list(spatial_results["comparison"].values())

        if sequential_improvements:
            summary["overall_performance"]["sequential_read_improvement"] = {
                "mean": statistics.mean(sequential_improvements),
                "min": min(sequential_improvements),
                "max": max(sequential_improvements),
            }

        if random_improvements:
            summary["overall_performance"]["random_read_improvement"] = {
                "mean": statistics.mean(random_improvements),
                "min": min(random_improvements),
                "max": max(random_improvements),
            }

        if spatial_improvements:
            summary["overall_performance"]["spatial_query_improvement"] = {
                "mean": statistics.mean(spatial_improvements),
                "min": min(spatial_improvements),
                "max": max(spatial_improvements),
            }

        # 关键发现
        if sequential_improvements:
            avg_seq_improvement = statistics.mean(sequential_improvements)
            if avg_seq_improvement > 0:
                summary["key_findings"].append(
                    f"顺序读取性能平均提升 {avg_seq_improvement:.1f}%"
                )
            else:
                summary["key_findings"].append(
                    f"顺序读取性能平均下降 {abs(avg_seq_improvement):.1f}%"
                )

        if random_improvements:
            avg_rand_improvement = statistics.mean(random_improvements)
            if avg_rand_improvement > 0:
                summary["key_findings"].append(
                    f"随机读取性能平均提升 {avg_rand_improvement:.1f}%"
                )
            else:
                summary["key_findings"].append(
                    f"随机读取性能平均下降 {abs(avg_rand_improvement):.1f}%"
                )

        if spatial_improvements:
            avg_spatial_improvement = statistics.mean(spatial_improvements)
            if avg_spatial_improvement > 0:
                summary["key_findings"].append(
                    f"空间查询性能平均提升 {avg_spatial_improvement:.1f}%"
                )
            else:
                summary["key_findings"].append(
                    f"空间查询性能平均下降 {abs(avg_spatial_improvement):.1f}%"
                )

        # 建议
        if sequential_improvements and statistics.mean(sequential_improvements) > 50:
            summary["recommendations"].append(
                "自定义格式在顺序读取场景下表现优异，适合批量数据处理"
            )

        if random_improvements and statistics.mean(random_improvements) > 50:
            summary["recommendations"].append(
                "自定义格式在随机读取场景下表现优异，适合随机访问场景"
            )

        if spatial_improvements and statistics.mean(spatial_improvements) > 50:
            summary["recommendations"].append(
                "自定义格式在空间查询场景下表现优异，适合空间分析应用"
            )

        return summary

    def run_complete_analysis(self):
        """运行完整的性能分析"""
        print("=" * 60)
        print("详细性能分析开始")
        print("=" * 60)

        # 确保数据已转换
        self.comparison.convert_shapefile_if_needed()

        # 1. 顺序读取性能测试
        sequential_results = self.test_sequential_read_performance()

        # 2. 随机读取性能测试
        random_results = self.test_random_read_performance()

        # 3. 空间查询性能测试
        spatial_results = self.test_spatial_query_performance()

        # 4. 生成图表
        self.generate_performance_charts(
            sequential_results, random_results, spatial_results
        )

        # 5. 保存结果
        self.save_results(sequential_results, random_results, spatial_results)

        # 6. 打印总结
        self.print_summary(sequential_results, random_results, spatial_results)

        print("\n" + "=" * 60)
        print("详细性能分析完成")
        print("=" * 60)

    def print_summary(
        self, sequential_results: Dict, random_results: Dict, spatial_results: Dict
    ):
        """打印测试总结"""
        print("\n" + "=" * 50)
        print("性能测试总结")
        print("=" * 50)

        # 顺序读取总结
        if sequential_results["comparison"]:
            improvements = list(sequential_results["comparison"].values())
            avg_improvement = statistics.mean(improvements)
            print(f"顺序读取性能: 平均提升 {avg_improvement:+.1f}%")
            print(f"  最佳提升: {max(improvements):+.1f}%")
            print(f"  最差提升: {min(improvements):+.1f}%")

        # 随机读取总结
        if random_results["comparison"]:
            improvements = list(random_results["comparison"].values())
            avg_improvement = statistics.mean(improvements)
            print(f"随机读取性能: 平均提升 {avg_improvement:+.1f}%")
            print(f"  最佳提升: {max(improvements):+.1f}%")
            print(f"  最差提升: {min(improvements):+.1f}%")

        # 空间查询总结
        if spatial_results["comparison"]:
            improvements = list(spatial_results["comparison"].values())
            avg_improvement = statistics.mean(improvements)
            print(f"空间查询性能: 平均提升 {avg_improvement:+.1f}%")
            print(f"  最佳提升: {max(improvements):+.1f}%")
            print(f"  最差提升: {min(improvements):+.1f}%")

        print(f"\n详细结果已保存到: {self.results_dir}")


def main():
    """主函数"""
    shapefile_path = "./data/test.shp"

    if not os.path.exists(shapefile_path):
        print(f"错误: 找不到Shapefile文件 {shapefile_path}")
        print("请确保数据文件存在")
        return

    # 创建详细性能分析对象
    analysis = DetailedPerformanceAnalysis(shapefile_path)

    # 运行完整分析
    analysis.run_complete_analysis()


if __name__ == "__main__":
    main()
