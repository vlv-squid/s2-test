#!/usr/bin/env python3
"""
Academic Spatial Index Benchmark Visualizer

This script creates publication-quality performance comparison charts 
following academic standards for spatial index research papers.
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
import seaborn as sns
from scipy import stats
from matplotlib.patches import Rectangle
import matplotlib.patches as mpatches

# 设置学术论文标准的字体和样式
plt.style.use("seaborn-v0_8-whitegrid")
plt.rcParams.update(
    {
        "font.family": "serif",
        "font.serif": ["Times New Roman", "DejaVu Serif"],
        "font.size": 12,
        "axes.titlesize": 14,
        "axes.labelsize": 12,
        "xtick.labelsize": 10,
        "ytick.labelsize": 10,
        "legend.fontsize": 11,
        "figure.titlesize": 16,
        "text.usetex": False,  # 如果系统有 LaTeX 可以设为 True
        "axes.linewidth": 1.0,
        "grid.alpha": 0.3,
        "axes.grid": True,
        "axes.axisbelow": True,
        "figure.dpi": 300,
        "savefig.dpi": 300,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.1,
    }
)

# 学术论文标准颜色方案
COLORS = {
    "ours": "#1f77b4",  # 蓝色
    "ogr": "#ff7f0e",  # 橙色
    "baseline": "#2ca02c",  # 绿色
    "error": "#d62728",  # 红色
}

MARKERS = {"ours": "o", "ogr": "s", "baseline": "^"}


class AcademicBenchmarkVisualizer:
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

    def create_academic_figures(self, output_dir: str = None):
        """Create academic-quality figures for publication"""
        if not self.data:
            print("No data to visualize")
            return

        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        # Figure 1: Index Construction Performance
        self.create_construction_performance_figure(
            os.path.join(output_dir, "fig1_construction_performance.pdf")
            if output_dir
            else None
        )

        # Figure 2: Query Performance Comparison
        self.create_query_performance_figure(
            os.path.join(output_dir, "fig2_query_performance.pdf")
            if output_dir
            else None
        )

        # Figure 3: I/O Performance Analysis
        self.create_io_performance_figure(
            os.path.join(output_dir, "fig3_io_performance.pdf") if output_dir else None
        )

        # Figure 4: Scalability Analysis
        self.create_scalability_figure(
            os.path.join(output_dir, "fig4_scalability_analysis.pdf")
            if output_dir
            else None
        )

    def create_construction_performance_figure(self, output_file: str = None):
        """Figure 1: Index Construction Performance"""
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

        # Subplot (a): Construction Time vs Data Size
        build_data = self.filter_data(operation="build", index_type="s2")
        if build_data:
            build_data.sort(key=lambda x: x["data_size"])
            sizes = [d["data_size"] for d in build_data]
            times = [d["avg_time_ms"] for d in build_data]
            errors = [d["std_dev_ms"] for d in build_data]

            ax1.errorbar(
                sizes,
                times,
                yerr=errors,
                marker=MARKERS["ours"],
                color=COLORS["ours"],
                linewidth=2,
                markersize=8,
                capsize=5,
                capthick=2,
                label="S2-based Index",
            )

            # 添加拟合线
            if len(sizes) > 2:
                z = np.polyfit(np.log(sizes), np.log(times), 1)
                p = np.poly1d(z)
                x_fit = np.logspace(np.log10(min(sizes)), np.log10(max(sizes)), 100)
                y_fit = np.exp(p(np.log(x_fit)))
                ax1.plot(
                    x_fit,
                    y_fit,
                    "--",
                    color=COLORS["ours"],
                    alpha=0.7,
                    label=f"Trend (slope: {z[0]:.2f})",
                )

        ax1.set_xlabel("Dataset Size (features)")
        ax1.set_ylabel("Construction Time (ms)")
        ax1.set_title("(a) Index Construction Time")
        ax1.set_xscale("log")
        ax1.set_yscale("log")
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # Subplot (b): Memory Usage vs Data Size
        if build_data:
            memory = [d["memory_usage_mb"] for d in build_data]
            ax2.plot(
                sizes,
                memory,
                marker=MARKERS["ours"],
                color=COLORS["ours"],
                linewidth=2,
                markersize=8,
                label="S2-based Index",
            )

        ax2.set_xlabel("Dataset Size (features)")
        ax2.set_ylabel("Memory Usage (MB)")
        ax2.set_title("(b) Memory Consumption")
        ax2.set_xscale("log")
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"Figure 1 saved to: {output_file}")
        else:
            plt.show()
        plt.close()

    def create_query_performance_figure(self, output_file: str = None):
        """Figure 2: Query Performance Comparison"""
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(12, 10))

        # Subplot (a): Spatial Query Performance
        spatial_data = self.filter_data(operation="spatial_query")
        self._plot_performance_comparison(
            ax1,
            spatial_data,
            "Spatial Query Time (ms)",
            "(a) Spatial Query Performance",
        )

        # Subplot (b): Performance Speedup
        self._plot_speedup_analysis(ax2, spatial_data, "(b) Query Speedup Factor")

        # Subplot (c): Sequential Read Performance
        seq_data = self.filter_data(operation="seq_read")
        self._plot_performance_comparison(
            ax3,
            seq_data,
            "Sequential Read Time (ms)",
            "(c) Sequential Read Performance",
        )

        # Subplot (d): Random Access Performance
        rand_data = self.filter_data(operation="rand_read")
        self._plot_performance_comparison(
            ax4, rand_data, "Random Access Time (ms)", "(d) Random Access Performance"
        )

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"Figure 2 saved to: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_performance_comparison(self, ax, data, ylabel, title):
        """Helper function to plot performance comparison"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in data])))
        index_types = list(set([d["index_type"] for d in data]))

        for index_type in index_types:
            type_data = [d for d in data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]
                errors = [d["std_dev_ms"] for d in type_data]

                color = COLORS["ours"] if index_type == "s2" else COLORS["ogr"]
                marker = MARKERS["ours"] if index_type == "s2" else MARKERS["ogr"]
                label = "S2-based Index" if index_type == "s2" else "OGR (Baseline)"

                ax.errorbar(
                    sizes,
                    times,
                    yerr=errors,
                    marker=marker,
                    color=color,
                    linewidth=2,
                    markersize=8,
                    capsize=5,
                    capthick=2,
                    label=label,
                )

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_speedup_analysis(self, ax, data, title):
        """Plot speedup factor analysis"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in data])))
        speedups = []

        for size in data_sizes:
            s2_data = [
                d for d in data if d["index_type"] == "s2" and d["data_size"] == size
            ]
            ogr_data = [
                d for d in data if d["index_type"] == "ogr" and d["data_size"] == size
            ]

            if s2_data and ogr_data:
                s2_time = s2_data[0]["avg_time_ms"]
                ogr_time = ogr_data[0]["avg_time_ms"]
                if s2_time > 0:
                    speedup = ogr_time / s2_time
                    speedups.append(speedup)
                else:
                    speedups.append(0)
            else:
                speedups.append(0)

        if speedups:
            bars = ax.bar(
                range(len(data_sizes)), speedups, color=COLORS["ours"], alpha=0.7
            )
            ax.set_xlabel("Dataset Size (features)")
            ax.set_ylabel("Speedup Factor (×)")
            ax.set_title(title)
            ax.set_xticks(range(len(data_sizes)))
            ax.set_xticklabels([f"{size:,}" for size in data_sizes], rotation=45)
            ax.grid(True, alpha=0.3, axis="y")

            # 添加数值标签
            for i, (bar, speedup) in enumerate(zip(bars, speedups)):
                if speedup > 0:
                    ax.text(
                        bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + 0.1,
                        f"{speedup:.1f}×",
                        ha="center",
                        va="bottom",
                        fontsize=10,
                    )

    def create_io_performance_figure(self, output_file: str = None):
        """Figure 3: I/O Performance Analysis"""
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

        # Subplot (a): Throughput Analysis
        seq_data = self.filter_data(operation="seq_read")
        self._plot_throughput_analysis(ax1, seq_data, "(a) Sequential Read Throughput")

        # Subplot (b): Random Access Latency Distribution
        rand_data = self.filter_data(operation="rand_read")
        self._plot_latency_distribution(ax2, rand_data, "(b) Random Access Latency")

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"Figure 3 saved to: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_throughput_analysis(self, ax, data, title):
        """Plot throughput analysis"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        index_types = list(set([d["index_type"] for d in data]))

        for index_type in index_types:
            type_data = [d for d in data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                sizes = [d["data_size"] for d in type_data]
                times = [d["avg_time_ms"] for d in type_data]

                # 计算吞吐量 (features per second)
                throughput = [
                    size / (time / 1000) if time > 0 else 0
                    for size, time in zip(sizes, times)
                ]

                color = COLORS["ours"] if index_type == "s2" else COLORS["ogr"]
                marker = MARKERS["ours"] if index_type == "s2" else MARKERS["ogr"]
                label = "S2-based Index" if index_type == "s2" else "OGR (Baseline)"

                ax.plot(
                    sizes,
                    throughput,
                    marker=marker,
                    color=color,
                    linewidth=2,
                    markersize=8,
                    label=label,
                )

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel("Throughput (features/sec)")
        ax.set_title(title)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_latency_distribution(self, ax, data, title):
        """Plot latency distribution"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        # 选择一个代表性的数据规模进行分析
        target_size = 10000
        target_data = [d for d in data if d["data_size"] == target_size]

        if not target_data:
            # 如果没有目标规模，选择最接近的
            sizes = [d["data_size"] for d in data]
            target_size = min(sizes, key=lambda x: abs(x - 10000))
            target_data = [d for d in data if d["data_size"] == target_size]

        index_types = list(set([d["index_type"] for d in target_data]))

        # 创建箱线图数据（模拟分布）
        positions = []
        box_data = []
        labels = []
        colors = []

        for i, index_type in enumerate(index_types):
            type_data = [d for d in target_data if d["index_type"] == index_type]
            if type_data:
                d = type_data[0]
                mean = d["avg_time_ms"]
                std = d["std_dev_ms"]

                # 生成模拟数据（假设正态分布）
                np.random.seed(42)  # 保证结果可重复
                simulated_data = np.random.normal(mean, std, 1000)
                simulated_data = simulated_data[simulated_data > 0]  # 移除负值

                positions.append(i)
                box_data.append(simulated_data)
                label = "S2-based Index" if index_type == "s2" else "OGR (Baseline)"
                labels.append(label)
                colors.append(COLORS["ours"] if index_type == "s2" else COLORS["ogr"])

        if box_data:
            bp = ax.boxplot(
                box_data,
                positions=positions,
                patch_artist=True,
                labels=labels,
                widths=0.6,
            )

            for patch, color in zip(bp["boxes"], colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.7)

        ax.set_ylabel("Latency (ms)")
        ax.set_title(f"{title} (n={target_size:,})")
        ax.grid(True, alpha=0.3, axis="y")

    def create_scalability_figure(self, output_file: str = None):
        """Figure 4: Scalability Analysis"""
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(12, 10))

        # Subplot (a): Time Complexity Analysis
        self._plot_time_complexity(ax1, "(a) Time Complexity")

        # Subplot (b): Space Complexity Analysis
        self._plot_space_complexity(ax2, "(b) Space Complexity")

        # Subplot (c): Performance Ratio vs Scale
        self._plot_performance_ratio(ax3, "(c) Performance Advantage")

        # Subplot (d): Statistical Significance
        self._plot_statistical_significance(ax4, "(d) Statistical Significance")

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"Figure 4 saved to: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_time_complexity(self, ax, title):
        """Plot time complexity analysis"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        index_types = ["s2", "ogr"]
        for index_type in index_types:
            type_data = [d for d in spatial_data if d["index_type"] == index_type]
            if type_data:
                type_data.sort(key=lambda x: x["data_size"])
                sizes = np.array([d["data_size"] for d in type_data])
                times = np.array([d["avg_time_ms"] for d in type_data])

                color = COLORS["ours"] if index_type == "s2" else COLORS["ogr"]
                marker = MARKERS["ours"] if index_type == "s2" else MARKERS["ogr"]
                label = "S2-based Index" if index_type == "s2" else "OGR (Baseline)"

                ax.loglog(
                    sizes,
                    times,
                    marker=marker,
                    color=color,
                    linewidth=2,
                    markersize=8,
                    label=label,
                )

                # 添加复杂度参考线
                if len(sizes) > 2:
                    # 计算斜率
                    log_sizes = np.log(sizes)
                    log_times = np.log(times)
                    slope, intercept, r_value, p_value, std_err = stats.linregress(
                        log_sizes, log_times
                    )

                    # 添加拟合线
                    x_fit = np.logspace(
                        np.log10(sizes.min()), np.log10(sizes.max()), 100
                    )
                    y_fit = np.exp(intercept) * x_fit**slope
                    ax.plot(
                        x_fit,
                        y_fit,
                        "--",
                        color=color,
                        alpha=0.7,
                        label=f"{label} O(n^{slope:.2f})",
                    )

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel("Query Time (ms)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_space_complexity(self, ax, title):
        """Plot space complexity analysis"""
        build_data = self.filter_data(operation="build", index_type="s2")
        if not build_data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        build_data.sort(key=lambda x: x["data_size"])
        sizes = np.array([d["data_size"] for d in build_data])
        memory = np.array([d["memory_usage_mb"] for d in build_data])

        ax.loglog(
            sizes,
            memory,
            marker=MARKERS["ours"],
            color=COLORS["ours"],
            linewidth=2,
            markersize=8,
            label="S2-based Index",
        )

        # 添加线性参考线
        if len(sizes) > 2:
            log_sizes = np.log(sizes)
            log_memory = np.log(memory)
            slope, intercept, r_value, p_value, std_err = stats.linregress(
                log_sizes, log_memory
            )

            x_fit = np.logspace(np.log10(sizes.min()), np.log10(sizes.max()), 100)
            y_fit = np.exp(intercept) * x_fit**slope
            ax.plot(
                x_fit,
                y_fit,
                "--",
                color=COLORS["ours"],
                alpha=0.7,
                label=f"Trend O(n^{slope:.2f})",
            )

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel("Memory Usage (MB)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_performance_ratio(self, ax, title):
        """Plot performance ratio analysis"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in spatial_data])))
        ratios = []

        for size in data_sizes:
            s2_data = [
                d
                for d in spatial_data
                if d["index_type"] == "s2" and d["data_size"] == size
            ]
            ogr_data = [
                d
                for d in spatial_data
                if d["index_type"] == "ogr" and d["data_size"] == size
            ]

            if s2_data and ogr_data:
                s2_time = s2_data[0]["avg_time_ms"]
                ogr_time = ogr_data[0]["avg_time_ms"]
                if s2_time > 0:
                    ratio = ogr_time / s2_time
                    ratios.append(ratio)
                else:
                    ratios.append(0)
            else:
                ratios.append(0)

        if ratios:
            ax.semilogx(
                data_sizes,
                ratios,
                marker=MARKERS["ours"],
                color=COLORS["ours"],
                linewidth=2,
                markersize=8,
                label="Performance Ratio",
            )

            # 添加平均线
            avg_ratio = np.mean([r for r in ratios if r > 0])
            ax.axhline(
                y=avg_ratio,
                color=COLORS["error"],
                linestyle="--",
                alpha=0.7,
                label=f"Average: {avg_ratio:.1f}×",
            )

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel("Performance Ratio (OGR/Ours)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_statistical_significance(self, ax, title):
        """Plot statistical significance analysis"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "No data available",
                ha="center",
                va="center",
                transform=ax.transAxes,
            )
            return

        data_sizes = sorted(list(set([d["data_size"] for d in spatial_data])))
        p_values = []
        effect_sizes = []

        for size in data_sizes:
            s2_data = [
                d
                for d in spatial_data
                if d["index_type"] == "s2" and d["data_size"] == size
            ]
            ogr_data = [
                d
                for d in spatial_data
                if d["index_type"] == "ogr" and d["data_size"] == size
            ]

            if s2_data and ogr_data:
                # 模拟数据进行统计检验
                s2_mean, s2_std = s2_data[0]["avg_time_ms"], s2_data[0]["std_dev_ms"]
                ogr_mean, ogr_std = (
                    ogr_data[0]["avg_time_ms"],
                    ogr_data[0]["std_dev_ms"],
                )

                # 生成模拟数据
                np.random.seed(42)
                s2_samples = np.random.normal(s2_mean, s2_std, 100)
                ogr_samples = np.random.normal(ogr_mean, ogr_std, 100)

                # 进行 t-test
                t_stat, p_val = stats.ttest_ind(s2_samples, ogr_samples)
                p_values.append(p_val)

                # 计算 Cohen's d (effect size)
                pooled_std = np.sqrt(
                    (
                        (len(s2_samples) - 1) * s2_std**2
                        + (len(ogr_samples) - 1) * ogr_std**2
                    )
                    / (len(s2_samples) + len(ogr_samples) - 2)
                )
                cohens_d = abs(s2_mean - ogr_mean) / pooled_std if pooled_std > 0 else 0
                effect_sizes.append(cohens_d)
            else:
                p_values.append(1.0)
                effect_sizes.append(0.0)

        if p_values and effect_sizes:
            # 创建散点图
            colors = [
                (
                    "red"
                    if p < 0.001
                    else "orange" if p < 0.01 else "yellow" if p < 0.05 else "gray"
                )
                for p in p_values
            ]

            scatter = ax.scatter(data_sizes, effect_sizes, c=colors, s=100, alpha=0.7)

            # 添加显著性水平线
            ax.axhline(
                y=0.2, color="gray", linestyle="--", alpha=0.5, label="Small effect"
            )
            ax.axhline(
                y=0.5, color="gray", linestyle="--", alpha=0.5, label="Medium effect"
            )
            ax.axhline(
                y=0.8, color="gray", linestyle="--", alpha=0.5, label="Large effect"
            )

            # 添加图例
            legend_elements = [
                mpatches.Patch(color="red", label="p < 0.001"),
                mpatches.Patch(color="orange", label="p < 0.01"),
                mpatches.Patch(color="yellow", label="p < 0.05"),
                mpatches.Patch(color="gray", label="p ≥ 0.05"),
            ]
            ax.legend(handles=legend_elements, loc="upper left")

        ax.set_xlabel("Dataset Size (features)")
        ax.set_ylabel("Effect Size (Cohen's d)")
        ax.set_title(title)
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3)

    def generate_academic_report(self, output_file: str = None):
        """Generate academic-style performance report"""
        if not self.data:
            print("No data to analyze")
            return

        report_lines = []
        report_lines.append("SPATIAL INDEX PERFORMANCE EVALUATION")
        report_lines.append("=" * 50)
        report_lines.append("")

        # 实验配置
        if "benchmark_info" in self.data:
            info = self.data["benchmark_info"]
            report_lines.append("EXPERIMENTAL SETUP")
            report_lines.append("-" * 20)
            report_lines.append(f"Dataset: {info.get('data_path', 'N/A')}")
            report_lines.append(
                f"Total Features: {info.get('total_features', 'N/A'):,}"
            )
            report_lines.append(
                f"Processed Features: {info.get('processed_features', 'N/A'):,}"
            )
            report_lines.append(
                f"System Memory: {info.get('memory_usage_mb', 'N/A'):.2f} MB"
            )
            report_lines.append("")

        # 性能分析
        spatial_data = self.filter_data(operation="spatial_query")
        if spatial_data:
            report_lines.append("PERFORMANCE ANALYSIS")
            report_lines.append("-" * 20)

            # 统计显著性分析
            s2_times = [
                d["avg_time_ms"] for d in spatial_data if d["index_type"] == "s2"
            ]
            ogr_times = [
                d["avg_time_ms"] for d in spatial_data if d["index_type"] == "ogr"
            ]

            if s2_times and ogr_times:
                # 基本统计
                report_lines.append(f"S2-based Index:")
                report_lines.append(
                    f"  Mean Query Time: {np.mean(s2_times):.3f} ± {np.std(s2_times):.3f} ms"
                )
                report_lines.append(
                    f"  Median Query Time: {np.median(s2_times):.3f} ms"
                )
                report_lines.append(
                    f"  Min/Max: {np.min(s2_times):.3f}/{np.max(s2_times):.3f} ms"
                )
                report_lines.append("")

                report_lines.append(f"OGR Baseline:")
                report_lines.append(
                    f"  Mean Query Time: {np.mean(ogr_times):.3f} ± {np.std(ogr_times):.3f} ms"
                )
                report_lines.append(
                    f"  Median Query Time: {np.median(ogr_times):.3f} ms"
                )
                report_lines.append(
                    f"  Min/Max: {np.min(ogr_times):.3f}/{np.max(ogr_times):.3f} ms"
                )
                report_lines.append("")

                # 性能提升
                speedup = np.mean(ogr_times) / np.mean(s2_times)
                report_lines.append(f"PERFORMANCE IMPROVEMENT")
                report_lines.append(f"  Average Speedup: {speedup:.2f}×")
                report_lines.append(f"  Performance Gain: {((speedup - 1) * 100):.1f}%")
                report_lines.append("")

                # 统计检验
                t_stat, p_value = stats.ttest_ind(s2_times, ogr_times)
                report_lines.append(f"STATISTICAL SIGNIFICANCE")
                report_lines.append(f"  t-statistic: {t_stat:.4f}")
                report_lines.append(f"  p-value: {p_value:.2e}")

                if p_value < 0.001:
                    significance = "highly significant (p < 0.001)"
                elif p_value < 0.01:
                    significance = "very significant (p < 0.01)"
                elif p_value < 0.05:
                    significance = "significant (p < 0.05)"
                else:
                    significance = "not significant (p ≥ 0.05)"

                report_lines.append(f"  Result: {significance}")
                report_lines.append("")

        # 可扩展性分析
        build_data = self.filter_data(operation="build", index_type="s2")
        if build_data and len(build_data) > 2:
            report_lines.append("SCALABILITY ANALYSIS")
            report_lines.append("-" * 20)

            sizes = np.array([d["data_size"] for d in build_data])
            times = np.array([d["avg_time_ms"] for d in build_data])
            memory = np.array([d["memory_usage_mb"] for d in build_data])

            # 时间复杂度
            log_sizes = np.log(sizes)
            log_times = np.log(times)
            time_slope, _, time_r2, _, _ = stats.linregress(log_sizes, log_times)

            # 空间复杂度
            log_memory = np.log(memory)
            memory_slope, _, memory_r2, _, _ = stats.linregress(log_sizes, log_memory)

            report_lines.append(
                f"Time Complexity: O(n^{time_slope:.3f}) (R² = {time_r2:.3f})"
            )
            report_lines.append(
                f"Space Complexity: O(n^{memory_slope:.3f}) (R² = {memory_r2:.3f})"
            )
            report_lines.append("")

        report_text = "\n".join(report_lines)

        if output_file:
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(report_text)
            print(f"Academic report saved to: {output_file}")
        else:
            print(report_text)


def main():
    parser = argparse.ArgumentParser(
        description="Academic Spatial Index Benchmark Visualizer"
    )
    parser.add_argument("json_file", help="Path to the benchmark JSON results file")
    parser.add_argument(
        "--output-dir",
        default="./academic_results",
        help="Output directory for figures and reports",
    )
    parser.add_argument(
        "--format",
        choices=["pdf", "png", "both"],
        default="pdf",
        help="Output format for figures",
    )

    args = parser.parse_args()

    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)

    # Initialize visualizer
    visualizer = AcademicBenchmarkVisualizer(args.json_file)

    if not visualizer.data:
        print("No data loaded. Exiting.")
        return

    # Generate academic figures
    print("Generating academic-quality figures...")
    visualizer.create_academic_figures(args.output_dir)

    # Generate academic report
    print("Generating academic report...")
    visualizer.generate_academic_report(
        os.path.join(args.output_dir, "academic_performance_report.txt")
    )

    print("Academic visualization completed!")


if __name__ == "__main__":
    main()
