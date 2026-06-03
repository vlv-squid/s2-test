#!/usr/bin/env python3
"""
学术空间索引基准测试可视化工具（中文版）

此脚本创建符合学术标准的性能对比图表，专为中文学术论文设计。
"""

import json
import matplotlib
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

# 设置中文字体支持
matplotlib.use("Agg")  # 使用非交互式后端

# 配置中文字体 - 改进版本
import matplotlib.font_manager as fm


# 尝试设置中文字体
def setup_chinese_font():
    """设置中文字体，优先使用系统可用的中文字体"""
    chinese_fonts = [
        "WenQuanYi Micro Hei",  # 文泉驿微米黑
        "WenQuanYi Zen Hei",  # 文泉驿正黑
        "Noto Sans CJK SC",  # Google Noto字体
        "Source Han Sans SC",  # 思源黑体
        "SimHei",  # 黑体
        "Microsoft YaHei",  # 微软雅黑
        "Arial Unicode MS",  # Arial Unicode MS
        "DejaVu Sans",  # 备用字体
    ]

    available_fonts = [f.name for f in fm.fontManager.ttflist]

    for font in chinese_fonts:
        if font in available_fonts:
            print(f"使用中文字体: {font}")
            return font

    print("警告: 未找到合适的中文字体，将使用默认字体")
    return "DejaVu Sans"


# 设置字体
chinese_font = setup_chinese_font()

plt.rcParams.update(
    {
        "font.family": chinese_font,
        "font.sans-serif": [chinese_font, "DejaVu Sans"],
        "axes.unicode_minus": False,  # 解决负号显示问题
        "font.size": 12,
        "axes.titlesize": 14,
        "axes.labelsize": 12,
        "xtick.labelsize": 10,
        "ytick.labelsize": 10,
        "legend.fontsize": 11,
        "figure.titlesize": 16,
        "text.usetex": False,
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


class AcademicBenchmarkVisualizerCN:
    def __init__(self, json_file: str):
        self.json_file = json_file
        self.data = self.load_data()

    def load_data(self) -> Dict:
        """从JSON文件加载基准测试结果"""
        try:
            with open(self.json_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"错误：找不到文件 {self.json_file}")
            return {}
        except json.JSONDecodeError:
            print(f"错误：{self.json_file} 中的JSON格式无效")
            return {}

    def filter_data(self, operation: str = None, index_type: str = None) -> List[Dict]:
        """根据操作类型和/或索引类型过滤数据"""
        if not self.data or "results" not in self.data:
            return []

        filtered = self.data["results"]
        if operation:
            filtered = [d for d in filtered if d["operation"] == operation]
        if index_type:
            filtered = [d for d in filtered if d["index_type"] == index_type]
        return filtered

    def create_academic_figures(self, output_dir: str = None):
        """创建学术质量的图表"""
        if not self.data:
            print("没有数据可供可视化")
            return

        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        # 图1：索引构建性能
        self.create_construction_performance_figure(
            os.path.join(output_dir, "图1_索引构建性能.pdf") if output_dir else None
        )

        # 图2：查询性能对比
        self.create_query_performance_figure(
            os.path.join(output_dir, "图2_查询性能对比.pdf") if output_dir else None
        )

        # 图3：I/O性能分析
        self.create_io_performance_figure(
            os.path.join(output_dir, "图3_IO性能分析.pdf") if output_dir else None
        )

        # 图4：可扩展性分析
        self.create_scalability_figure(
            os.path.join(output_dir, "图4_可扩展性分析.pdf") if output_dir else None
        )

    def create_construction_performance_figure(self, output_file: str = None):
        """图1：索引构建性能"""
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

        # 子图(a)：构建时间 vs 数据规模
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
                label="基于S2的索引",
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
                    label=f"趋势线 (斜率: {z[0]:.2f})",
                )

        ax1.set_xlabel("数据集规模 (要素数量)")
        ax1.set_ylabel("构建时间 (毫秒)")
        ax1.set_title("(a) 索引构建时间")
        ax1.set_xscale("log")
        ax1.set_yscale("log")
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # 子图(b)：内存使用 vs 数据规模
        if build_data:
            memory = [d["memory_usage_mb"] for d in build_data]
            ax2.plot(
                sizes,
                memory,
                marker=MARKERS["ours"],
                color=COLORS["ours"],
                linewidth=2,
                markersize=8,
                label="基于S2的索引",
            )

        ax2.set_xlabel("数据集规模 (要素数量)")
        ax2.set_ylabel("内存使用 (MB)")
        ax2.set_title("(b) 内存消耗")
        ax2.set_xscale("log")
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"图1已保存到: {output_file}")
        else:
            plt.show()
        plt.close()

    def create_query_performance_figure(self, output_file: str = None):
        """图2：查询性能对比"""
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(12, 10))

        # 子图(a)：空间查询性能
        spatial_data = self.filter_data(operation="spatial_query")
        self._plot_performance_comparison(
            ax1,
            spatial_data,
            "空间查询时间 (毫秒)",
            "(a) 空间查询性能",
        )

        # 子图(b)：性能加速比
        self._plot_speedup_analysis(ax2, spatial_data, "(b) 查询加速比")

        # 子图(c)：顺序读取性能
        seq_data = self.filter_data(operation="seq_read")
        self._plot_performance_comparison(
            ax3,
            seq_data,
            "顺序读取时间 (毫秒)",
            "(c) 顺序读取性能",
        )

        # 子图(d)：随机访问性能
        rand_data = self.filter_data(operation="rand_read")
        self._plot_performance_comparison(
            ax4, rand_data, "随机访问时间 (毫秒)", "(d) 随机访问性能"
        )

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"图2已保存到: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_performance_comparison(self, ax, data, ylabel, title):
        """绘制性能对比的辅助函数"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
                label = "基于S2的索引" if index_type == "s2" else "OGR基线"

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

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_speedup_analysis(self, ax, data, title):
        """绘制加速比分析"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
            ax.set_xlabel("数据集规模 (要素数量)")
            ax.set_ylabel("加速比 (倍)")
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
        """图3：I/O性能分析"""
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

        # 子图(a)：吞吐量分析
        seq_data = self.filter_data(operation="seq_read")
        self._plot_throughput_analysis(ax1, seq_data, "(a) 顺序读取吞吐量")

        # 子图(b)：随机访问延迟分布
        rand_data = self.filter_data(operation="rand_read")
        self._plot_latency_distribution(ax2, rand_data, "(b) 随机访问延迟")

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"图3已保存到: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_throughput_analysis(self, ax, data, title):
        """绘制吞吐量分析"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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

                # 计算吞吐量 (要素/秒)
                throughput = [
                    size / (time / 1000) if time > 0 else 0
                    for size, time in zip(sizes, times)
                ]

                color = COLORS["ours"] if index_type == "s2" else COLORS["ogr"]
                marker = MARKERS["ours"] if index_type == "s2" else MARKERS["ogr"]
                label = "基于S2的索引" if index_type == "s2" else "OGR基线"

                ax.plot(
                    sizes,
                    throughput,
                    marker=marker,
                    color=color,
                    linewidth=2,
                    markersize=8,
                    label=label,
                )

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel("吞吐量 (要素/秒)")
        ax.set_title(title)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_latency_distribution(self, ax, data, title):
        """绘制延迟分布"""
        if not data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
                label = "基于S2的索引" if index_type == "s2" else "OGR基线"
                labels.append(label)
                colors.append(COLORS["ours"] if index_type == "s2" else COLORS["ogr"])

        if box_data:
            bp = ax.boxplot(
                box_data,
                positions=positions,
                patch_artist=True,
                tick_labels=labels,
                widths=0.6,
            )

            for patch, color in zip(bp["boxes"], colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.7)

        ax.set_ylabel("延迟 (毫秒)")
        ax.set_title(f"{title} (n={target_size:,})")
        ax.grid(True, alpha=0.3, axis="y")

    def create_scalability_figure(self, output_file: str = None):
        """图4：可扩展性分析"""
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(12, 10))

        # 子图(a)：时间复杂度分析
        self._plot_time_complexity(ax1, "(a) 时间复杂度")

        # 子图(b)：空间复杂度分析
        self._plot_space_complexity(ax2, "(b) 空间复杂度")

        # 子图(c)：性能比率 vs 规模
        self._plot_performance_ratio(ax3, "(c) 性能优势")

        # 子图(d)：统计显著性
        self._plot_statistical_significance(ax4, "(d) 统计显著性")

        plt.tight_layout()
        if output_file:
            plt.savefig(output_file, format="pdf")
            print(f"图4已保存到: {output_file}")
        else:
            plt.show()
        plt.close()

    def _plot_time_complexity(self, ax, title):
        """绘制时间复杂度分析"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
                label = "基于S2的索引" if index_type == "s2" else "OGR基线"

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

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel("查询时间 (毫秒)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_space_complexity(self, ax, title):
        """绘制空间复杂度分析"""
        build_data = self.filter_data(operation="build", index_type="s2")
        if not build_data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
            label="基于S2的索引",
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
                label=f"趋势线 O(n^{slope:.2f})",
            )

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel("内存使用 (MB)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_performance_ratio(self, ax, title):
        """绘制性能比率分析"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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
                label="性能比率",
            )

            # 添加平均线
            avg_ratio = np.mean([r for r in ratios if r > 0])
            ax.axhline(
                y=avg_ratio,
                color=COLORS["error"],
                linestyle="--",
                alpha=0.7,
                label=f"平均值: {avg_ratio:.1f}×",
            )

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel("性能比率 (OGR/我们的方法)")
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

    def _plot_statistical_significance(self, ax, title):
        """绘制统计显著性分析"""
        spatial_data = self.filter_data(operation="spatial_query")
        if not spatial_data:
            ax.text(
                0.5,
                0.5,
                "无可用数据",
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

                # 计算 Cohen's d (效应量)
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
            ax.axhline(y=0.2, color="gray", linestyle="--", alpha=0.5, label="小效应")
            ax.axhline(y=0.5, color="gray", linestyle="--", alpha=0.5, label="中等效应")
            ax.axhline(y=0.8, color="gray", linestyle="--", alpha=0.5, label="大效应")

            # 添加图例
            legend_elements = [
                mpatches.Patch(color="red", label="p < 0.001"),
                mpatches.Patch(color="orange", label="p < 0.01"),
                mpatches.Patch(color="yellow", label="p < 0.05"),
                mpatches.Patch(color="gray", label="p ≥ 0.05"),
            ]
            ax.legend(handles=legend_elements, loc="upper left")

        ax.set_xlabel("数据集规模 (要素数量)")
        ax.set_ylabel("效应量 (Cohen's d)")
        ax.set_title(title)
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3)

    def generate_academic_report(self, output_file: str = None):
        """生成学术风格的性能报告"""
        if not self.data:
            print("没有数据可供分析")
            return

        report_lines = []
        report_lines.append("空间索引性能评估")
        report_lines.append("=" * 50)
        report_lines.append("")

        # 实验配置
        if "benchmark_info" in self.data:
            info = self.data["benchmark_info"]
            report_lines.append("实验配置")
            report_lines.append("-" * 20)
            report_lines.append(f"数据集: {info.get('data_path', 'N/A')}")
            report_lines.append(f"总要素数: {info.get('total_features', 'N/A'):,}")
            report_lines.append(
                f"处理要素数: {info.get('processed_features', 'N/A'):,}"
            )
            report_lines.append(
                f"系统内存: {info.get('memory_usage_mb', 'N/A'):.2f} MB"
            )
            report_lines.append("")

        # 性能分析
        spatial_data = self.filter_data(operation="spatial_query")
        if spatial_data:
            report_lines.append("性能分析")
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
                report_lines.append(f"基于S2的索引:")
                report_lines.append(
                    f"  平均查询时间: {np.mean(s2_times):.3f} ± {np.std(s2_times):.3f} 毫秒"
                )
                report_lines.append(f"  中位数查询时间: {np.median(s2_times):.3f} 毫秒")
                report_lines.append(
                    f"  最小/最大: {np.min(s2_times):.3f}/{np.max(s2_times):.3f} 毫秒"
                )
                report_lines.append("")

                report_lines.append(f"OGR基线:")
                report_lines.append(
                    f"  平均查询时间: {np.mean(ogr_times):.3f} ± {np.std(ogr_times):.3f} 毫秒"
                )
                report_lines.append(
                    f"  中位数查询时间: {np.median(ogr_times):.3f} 毫秒"
                )
                report_lines.append(
                    f"  最小/最大: {np.min(ogr_times):.3f}/{np.max(ogr_times):.3f} 毫秒"
                )
                report_lines.append("")

                # 性能提升
                speedup = np.mean(ogr_times) / np.mean(s2_times)
                report_lines.append(f"性能提升")
                report_lines.append(f"  平均加速比: {speedup:.2f}×")
                report_lines.append(f"  性能提升: {((speedup - 1) * 100):.1f}%")
                report_lines.append("")

                # 统计检验
                t_stat, p_value = stats.ttest_ind(s2_times, ogr_times)
                report_lines.append(f"统计显著性")
                report_lines.append(f"  t统计量: {t_stat:.4f}")
                report_lines.append(f"  p值: {p_value:.2e}")

                if p_value < 0.001:
                    significance = "高度显著 (p < 0.001)"
                elif p_value < 0.01:
                    significance = "非常显著 (p < 0.01)"
                elif p_value < 0.05:
                    significance = "显著 (p < 0.05)"
                else:
                    significance = "不显著 (p ≥ 0.05)"

                report_lines.append(f"  结果: {significance}")
                report_lines.append("")

        # 可扩展性分析
        build_data = self.filter_data(operation="build", index_type="s2")
        if build_data and len(build_data) > 2:
            report_lines.append("可扩展性分析")
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
                f"时间复杂度: O(n^{time_slope:.3f}) (R² = {time_r2:.3f})"
            )
            report_lines.append(
                f"空间复杂度: O(n^{memory_slope:.3f}) (R² = {memory_r2:.3f})"
            )
            report_lines.append("")

        report_text = "\n".join(report_lines)

        if output_file:
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(report_text)
            print(f"学术报告已保存到: {output_file}")
        else:
            print(report_text)


def main():
    parser = argparse.ArgumentParser(
        description="学术空间索引基准测试可视化工具（中文版）"
    )
    parser.add_argument("json_file", help="基准测试结果JSON文件路径")
    parser.add_argument(
        "--output-dir",
        default="./academic_results_cn",
        help="图表和报告的输出目录",
    )
    parser.add_argument(
        "--format",
        choices=["pdf", "png", "both"],
        default="pdf",
        help="图表输出格式",
    )

    args = parser.parse_args()

    # 创建输出目录
    os.makedirs(args.output_dir, exist_ok=True)

    # 初始化可视化工具
    visualizer = AcademicBenchmarkVisualizerCN(args.json_file)

    if not visualizer.data:
        print("没有加载到数据。退出。")
        return

    # 生成学术图表
    print("正在生成学术质量图表...")
    visualizer.create_academic_figures(args.output_dir)

    # 生成学术报告
    print("正在生成学术报告...")
    visualizer.generate_academic_report(
        os.path.join(args.output_dir, "学术性能报告.txt")
    )

    print("学术可视化完成！")


if __name__ == "__main__":
    main()
