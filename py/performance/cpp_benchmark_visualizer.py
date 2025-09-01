#!/usr/bin/env python3
"""
C++ Spatial Index Benchmark Visualizer

This script reads the JSON output from the C++ spatial index benchmark
and generates performance comparison charts similar to the Python version.
"""

import json
import matplotlib

matplotlib.use("Agg")  # 使用非交互式后端
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import argparse
import os
from typing import Dict, List, Tuple

# Set font for better compatibility
plt.rcParams["font.sans-serif"] = ["DejaVu Sans", "Arial", "Helvetica"]
plt.rcParams["axes.unicode_minus"] = False


class CppBenchmarkVisualizer:
    def __init__(self, json_file: str):
        self.json_file = json_file
        self.data = self.load_data()

    def load_data(self) -> Dict:
        """Load benchmark results from JSON file"""
        try:
            with open(self.json_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"Error: File {self.json_file} not found")
            return {}
        except json.JSONDecodeError:
            print(f"Error: Invalid JSON in {self.json_file}")
            return {}

    def filter_data(self, operation: str = None, index_type: str = None) -> List[Dict]:
        """Filter data by operation and/or index type"""
        if not self.data or "results" not in self.data:
            return []

        filtered = self.data["results"]

        if operation:
            filtered = [d for d in filtered if d["operation"] == operation]
        if index_type:
            filtered = [d for d in filtered if d["index_type"] == index_type]
        return filtered

    def create_performance_charts(self, output_file: str = None):
        """Create comprehensive performance comparison charts similar to Python version"""
        if not self.data:
            print("No data to visualize")
            return

        # Create figure with subplots
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(15, 12))
        fig.suptitle("C++ Spatial Index Performance Comparison", fontsize=16)

        # 1. 构建索引时间对比
        self.plot_build_time_comparison(ax1)

        # 2. 空间查询性能对比
        self.plot_query_time_comparison(ax2)

        # 3. 顺序读取性能对比
        self.plot_seq_read_comparison(ax3)

        # 4. 随机读取性能对比
        self.plot_rand_read_comparison(ax4)

        # 4. 性能提升对比
        self.plot_performance_improvement(ax4)

        plt.tight_layout()

        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches="tight")
            print(f"Charts saved to: {output_file}")
        else:
            plt.show()

    def plot_build_time_comparison(self, ax):
        """Plot build time comparison"""
        build_data = self.filter_data(operation="build")

        if not build_data:
            ax.text(
                0.5,
                0.5,
                "No build data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小和索引类型
        data_sizes = sorted(list(set([d["data_size"] for d in build_data])))
        index_types = list(set([d["index_type"] for d in build_data]))

        # 为每种索引类型绘制数据
        for index_type in index_types:
            type_data = [d for d in build_data if d["index_type"] == index_type]
            if type_data:
                # 按数据大小排序
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]

                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.plot(
                    sizes,
                    times,
                    "o-",
                    label=label,
                    linewidth=2,
                    markersize=6,
                )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Build Time (ms)")
        ax.set_title("Index Build Time Comparison")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")

    def plot_query_time_comparison(self, ax):
        """Plot query time comparison"""
        query_data = self.filter_data(operation="spatial_query")

        if not query_data:
            ax.text(
                0.5,
                0.5,
                "No query data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小和索引类型
        data_sizes = sorted(list(set([d["data_size"] for d in query_data])))
        index_types = list(set([d["index_type"] for d in query_data]))

        # 为每种索引类型绘制数据
        for index_type in index_types:
            type_data = [d for d in query_data if d["index_type"] == index_type]
            if type_data:
                # 按数据大小排序
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]

                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.plot(
                    sizes,
                    times,
                    "s-",
                    label=label,
                    linewidth=2,
                    markersize=6,
                )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Query Time (ms)")
        ax.set_title("Spatial Query Performance")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")
        ax.set_yscale("log")

    def plot_memory_usage_comparison(self, ax):
        """Plot memory usage comparison"""
        build_data = self.filter_data(operation="build")

        if not build_data:
            ax.text(
                0.5,
                0.5,
                "No memory data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小和索引类型
        data_sizes = sorted(list(set([d["data_size"] for d in build_data])))
        index_types = list(set([d["index_type"] for d in build_data]))

        # 为每种索引类型绘制数据
        for index_type in index_types:
            type_data = [d for d in build_data if d["index_type"] == index_type]
            if type_data:
                # 按数据大小排序
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                memory = [d["memory_usage_mb"] for d in type_data]

                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.plot(
                    sizes,
                    memory,
                    "^-",
                    label=label,
                    linewidth=2,
                    markersize=6,
                )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Memory Usage (MB)")
        ax.set_title("Memory Usage Comparison")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")
        ax.set_yscale("log")

    def plot_seq_read_comparison(self, ax):
        """Plot sequential read performance comparison"""
        seq_read_data = self.filter_data(operation="seq_read")

        if not seq_read_data:
            ax.text(
                0.5,
                0.5,
                "No sequential read data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小和索引类型
        data_sizes = sorted(list(set([d["data_size"] for d in seq_read_data])))
        index_types = list(set([d["index_type"] for d in seq_read_data]))

        # 为每种索引类型绘制数据
        for index_type in index_types:
            type_data = [d for d in seq_read_data if d["index_type"] == index_type]
            if type_data:
                # 按数据大小排序
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]

                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.plot(
                    sizes,
                    times,
                    "^-",
                    label=label,
                    linewidth=2,
                    markersize=6,
                )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Sequential Read Time (ms)")
        ax.set_title("Sequential Read Performance")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")
        ax.set_yscale("log")

    def plot_rand_read_comparison(self, ax):
        """Plot random read performance comparison"""
        rand_read_data = self.filter_data(operation="rand_read")

        if not rand_read_data:
            ax.text(
                0.5,
                0.5,
                "No random read data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小和索引类型
        data_sizes = sorted(list(set([d["data_size"] for d in rand_read_data])))
        index_types = list(set([d["index_type"] for d in rand_read_data]))

        # 为每种索引类型绘制数据
        for index_type in index_types:
            type_data = [d for d in rand_read_data if d["index_type"] == index_type]
            if type_data:
                # 按数据大小排序
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]

                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.plot(
                    sizes,
                    times,
                    "s-",
                    label=label,
                    linewidth=2,
                    markersize=6,
                )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Random Read Time (ms)")
        ax.set_title("Random Read Performance")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")
        ax.set_yscale("log")

    def plot_performance_improvement(self, ax):
        """Plot performance improvement over OGR"""
        query_data = self.filter_data(operation="spatial_query")

        if not query_data:
            ax.text(
                0.5,
                0.5,
                "No performance data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 获取数据大小
        data_sizes = sorted(list(set([d["data_size"] for d in query_data])))

        # 计算性能提升
        improvements = []
        for size in data_sizes:
            ogr_data = [
                d
                for d in query_data
                if d["index_type"] == "ogr" and d["data_size"] == size
            ]
            s2_data = [
                d
                for d in query_data
                if d["index_type"] == "s2" and d["data_size"] == size
            ]

            if ogr_data and s2_data:
                ogr_time = ogr_data[0]["avg_time_ms"]
                s2_time = s2_data[0]["avg_time_ms"]

                if ogr_time > 0:
                    improvement = ((ogr_time - s2_time) / ogr_time) * 100
                    improvements.append(improvement)
                else:
                    improvements.append(0)
            else:
                improvements.append(0)

        if improvements:
            ax.bar(data_sizes, improvements, alpha=0.7, color="green")
            ax.set_xlabel("Test Scale (Features)")
            ax.set_ylabel("Performance Improvement (%)")
            ax.set_title("Ours vs OGR Performance Improvement")
            ax.grid(True, alpha=0.3)
            ax.set_xscale("log")

            # 添加数值标签
            for i, (size, improvement) in enumerate(zip(data_sizes, improvements)):
                ax.text(
                    size,
                    improvement + 1,
                    f"{improvement:.1f}%",
                    ha="center",
                    va="bottom",
                    fontsize=8,
                )
        else:
            ax.text(
                0.5,
                0.5,
                "No improvement data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )

    def create_detailed_analysis(self, output_file: str = None):
        """Create detailed performance analysis"""
        if not self.data:
            print("No data to analyze")
            return

        # Create detailed analysis figure
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
        fig.suptitle(
            "Detailed C++ Spatial Index Analysis", fontsize=16, fontweight="bold"
        )

        # 1. 构建时间详细分析
        self.plot_build_time_detailed(ax1)

        # 2. 查询时间详细分析
        self.plot_query_time_detailed(ax2)

        # 3. 顺序读取详细分析
        self.plot_seq_read_detailed(ax3)

        # 4. 随机读取详细分析
        self.plot_rand_read_detailed(ax4)

        # 3. 内存效率分析
        self.plot_memory_efficiency(ax3)

        # 4. 结果数量对比
        self.plot_result_count_comparison(ax4)

        plt.tight_layout()

        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches="tight")
            print(f"Detailed analysis saved to: {output_file}")
        else:
            plt.show()

    def plot_build_time_detailed(self, ax):
        """Detailed build time analysis"""
        build_data = self.filter_data(operation="build")

        if not build_data:
            ax.text(
                0.5,
                0.5,
                "No build data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in build_data])))
        index_types = list(set([d["index_type"] for d in build_data]))

        x = np.arange(len(data_sizes))
        width = 0.8 / len(index_types) if index_types else 0.8

        for i, index_type in enumerate(index_types):
            type_data = [d for d in build_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                times = [d["avg_time_ms"] for d in type_data]
                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.bar(x + i * width, times, width, label=label, alpha=0.8)

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Build Time (ms)")
        ax.set_title("Detailed Build Time Analysis")
        ax.set_xticks(x + width / 2)
        ax.set_xticklabels(data_sizes)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def plot_query_time_detailed(self, ax):
        """Detailed query time analysis"""
        query_data = self.filter_data(operation="spatial_query")

        if not query_data:
            ax.text(
                0.5,
                0.5,
                "No query data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in query_data])))
        index_types = list(set([d["index_type"] for d in query_data]))

        x = np.arange(len(data_sizes))
        width = 0.8 / len(index_types) if index_types else 0.8

        for i, index_type in enumerate(index_types):
            type_data = [d for d in query_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                times = [d["avg_time_ms"] for d in type_data]
                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.bar(x + i * width, times, width, label=label, alpha=0.8)

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Query Time (ms)")
        ax.set_title("Detailed Query Time Analysis")
        ax.set_xticks(x + width / 2)
        ax.set_xticklabels(data_sizes)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_yscale("log")

    def plot_seq_read_detailed(self, ax):
        """Detailed sequential read analysis"""
        seq_read_data = self.filter_data(operation="seq_read")

        if not seq_read_data:
            ax.text(
                0.5,
                0.5,
                "No sequential read data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in seq_read_data])))
        index_types = list(set([d["index_type"] for d in seq_read_data]))

        x = np.arange(len(data_sizes))
        width = 0.8 / len(index_types) if index_types else 0.8

        for i, index_type in enumerate(index_types):
            type_data = [d for d in seq_read_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                times = [d["avg_time_ms"] for d in type_data]
                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.bar(x + i * width, times, width, label=label, alpha=0.8)

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Sequential Read Time (ms)")
        ax.set_title("Detailed Sequential Read Analysis")
        ax.set_xticks(x + width / 2)
        ax.set_xticklabels(data_sizes)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_yscale("log")

    def plot_rand_read_detailed(self, ax):
        """Detailed random read analysis"""
        rand_read_data = self.filter_data(operation="rand_read")

        if not rand_read_data:
            ax.text(
                0.5,
                0.5,
                "No random read data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in rand_read_data])))
        index_types = list(set([d["index_type"] for d in rand_read_data]))

        x = np.arange(len(data_sizes))
        width = 0.8 / len(index_types) if index_types else 0.8

        for i, index_type in enumerate(index_types):
            type_data = [d for d in rand_read_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                times = [d["avg_time_ms"] for d in type_data]
                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.bar(x + i * width, times, width, label=label, alpha=0.8)

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Random Read Time (ms)")
        ax.set_title("Detailed Random Read Analysis")
        ax.set_xticks(x + width / 2)
        ax.set_xticklabels(data_sizes)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_yscale("log")

    def plot_memory_efficiency(self, ax):
        """Memory efficiency analysis"""
        # 使用所有操作的数据来计算内存效率，但主要关注 S2 的索引内存
        all_data = self.data.get("results", [])

        if not all_data:
            ax.text(
                0.5,
                0.5,
                "No memory data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 按数据大小分组，计算平均内存使用
        data_sizes = sorted(list(set([d["data_size"] for d in all_data])))

        # 为 S2 计算索引内存效率（仅使用 build 操作）
        s2_build_data = [
            d for d in all_data if d["index_type"] == "s2" and d["operation"] == "build"
        ]
        if s2_build_data:
            s2_build_data.sort(key=lambda x: x["data_size"])
            s2_sizes = [d["data_size"] for d in s2_build_data]
            s2_memory = [d["memory_usage_mb"] for d in s2_build_data]

            # 计算每要素的索引内存使用（转换为 KB）
            memory_per_feature_kb = [
                mem * 1024 / size for mem, size in zip(s2_memory, s2_sizes)
            ]

            ax.plot(
                s2_sizes,
                memory_per_feature_kb,
                "o-",
                label="Ours (Index Memory)",
                linewidth=2,
                markersize=6,
                color="blue",
            )

        # 为 OGR 计算进程内存使用（使用任意操作的数据）
        ogr_data = [d for d in all_data if d["index_type"] == "ogr"]
        if ogr_data:
            # 按数据大小分组，取平均内存
            ogr_memory_by_size = {}
            for d in ogr_data:
                size = d["data_size"]
                if size not in ogr_memory_by_size:
                    ogr_memory_by_size[size] = []
                ogr_memory_by_size[size].append(d["memory_usage_mb"])

            ogr_sizes = sorted(ogr_memory_by_size.keys())
            ogr_avg_memory = [np.mean(ogr_memory_by_size[size]) for size in ogr_sizes]

            # 计算每要素的进程内存使用（转换为 KB）
            ogr_memory_per_feature_kb = [
                mem * 1024 / size for mem, size in zip(ogr_avg_memory, ogr_sizes)
            ]

            ax.plot(
                ogr_sizes,
                ogr_memory_per_feature_kb,
                "s-",
                label="OGR (Process Memory)",
                linewidth=2,
                markersize=6,
                color="red",
            )

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Memory per Feature (KB)")
        ax.set_title("Memory Efficiency Analysis")
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale("log")
        ax.set_yscale("log")

        # 添加说明文字
        ax.text(
            0.02,
            0.98,
            "Lower is better",
            transform=ax.transAxes,
            fontsize=10,
            verticalalignment="top",
            bbox=dict(boxstyle="round", facecolor="wheat", alpha=0.8),
        )

    def plot_result_count_comparison(self, ax):
        """Result count comparison"""
        query_data = self.filter_data(operation="spatial_query")

        if not query_data:
            ax.text(
                0.5,
                0.5,
                "No result data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in query_data])))
        index_types = list(set([d["index_type"] for d in query_data]))

        x = np.arange(len(data_sizes))
        width = 0.8 / len(index_types) if index_types else 0.8

        for i, index_type in enumerate(index_types):
            type_data = [d for d in query_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                counts = [d["result_count"] for d in type_data]
                # 将S2标签改为Ours
                label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                ax.bar(x + i * width, counts, width, label=label, alpha=0.8)

        ax.set_xlabel("Test Scale (Features)")
        ax.set_ylabel("Result Count")
        ax.set_title("Query Result Count Comparison")
        ax.set_xticks(x + width / 2)
        ax.set_xticklabels(data_sizes)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def generate_summary_report(self, output_file: str = None):
        """Generate summary report"""
        if not self.data:
            print("No data to analyze")
            return

        report_lines = []
        report_lines.append("C++ Spatial Index Benchmark Summary Report")
        report_lines.append("=" * 50)
        report_lines.append("")

        # 基本信息
        if "benchmark_info" in self.data:
            info = self.data["benchmark_info"]
            report_lines.append(f"Data Path: {info.get('data_path', 'N/A')}")
            report_lines.append(
                f"Total Features: {info.get('total_features', 'N/A'):,}"
            )
            report_lines.append(
                f"Processed Features: {info.get('processed_features', 'N/A'):,}"
            )
            report_lines.append(
                f"Memory Usage: {info.get('memory_usage_mb', 'N/A'):.2f} MB"
            )
            report_lines.append("")

        # 性能分析
        query_data = self.filter_data(operation="spatial_query")
        if query_data:
            report_lines.append("Performance Analysis:")
            report_lines.append("-" * 20)

            # 按索引类型分组
            index_types = list(set([d["index_type"] for d in query_data]))
            for index_type in index_types:
                type_data = [d for d in query_data if d["index_type"] == index_type]
                if type_data:
                    avg_time = np.mean([d["avg_time_ms"] for d in type_data])
                    min_time = min([d["avg_time_ms"] for d in type_data])
                    max_time = max([d["avg_time_ms"] for d in type_data])

                    # 将S2标签改为Ours
                    label = "Ours" if index_type.upper() == "S2" else index_type.upper()
                    report_lines.append(f"{label} Index:")
                    report_lines.append(f"  Average Query Time: {avg_time:.2f} ms")
                    report_lines.append(f"  Min Query Time: {min_time:.2f} ms")
                    report_lines.append(f"  Max Query Time: {max_time:.2f} ms")
                    report_lines.append("")

        # 性能提升分析
        if query_data:
            s2_data = [d for d in query_data if d["index_type"] == "s2"]
            ogr_data = [d for d in query_data if d["index_type"] == "ogr"]

            if s2_data and ogr_data:
                s2_avg = np.mean([d["avg_time_ms"] for d in s2_data])
                ogr_avg = np.mean([d["avg_time_ms"] for d in ogr_data])

                if ogr_avg > 0:
                    improvement = ((ogr_avg - s2_avg) / ogr_avg) * 100
                    report_lines.append(f"Performance Improvement:")
                    report_lines.append(f"  Ours vs OGR: {improvement:.1f}% faster")
                    report_lines.append("")

        report_text = "\n".join(report_lines)

        if output_file:
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(report_text)
            print(f"Summary report saved to: {output_file}")
        else:
            print(report_text)


def main():
    parser = argparse.ArgumentParser(
        description="C++ Spatial Index Benchmark Visualizer"
    )
    parser.add_argument("json_file", help="Path to the benchmark JSON results file")
    parser.add_argument(
        "--output-dir",
        default="./benchmark_results",
        help="Output directory for charts and reports",
    )
    parser.add_argument(
        "--detailed", action="store_true", help="Generate detailed analysis charts"
    )
    parser.add_argument(
        "--summary", action="store_true", help="Generate summary report"
    )

    args = parser.parse_args()

    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)

    # Initialize visualizer
    visualizer = CppBenchmarkVisualizer(args.json_file)

    if not visualizer.data:
        print("No data loaded. Exiting.")
        return

    # Generate charts
    print("Generating performance charts...")
    visualizer.create_performance_charts(
        os.path.join(args.output_dir, "cpp_benchmark_charts.png")
    )

    if args.detailed:
        print("Generating detailed analysis...")
        visualizer.create_detailed_analysis(
            os.path.join(args.output_dir, "cpp_benchmark_detailed_analysis.png")
        )

    if args.summary:
        print("Generating summary report...")
        visualizer.generate_summary_report(
            os.path.join(args.output_dir, "cpp_benchmark_summary.txt")
        )

    print("Visualization completed!")


if __name__ == "__main__":
    main()
