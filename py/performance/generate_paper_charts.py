#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成论文图表的脚本 - 修正版
"""

import json
import matplotlib.pyplot as plt
import numpy as np
import matplotlib
from scipy import stats

# 设置中文字体
matplotlib.rcParams["font.family"] = [
    "DejaVu Sans",
    "Arial Unicode MS",
    "SimHei",
    "WenQuanYi Micro Hei",
]
matplotlib.rcParams["axes.unicode_minus"] = False

# 尝试设置中文字体
try:
    import matplotlib.font_manager as fm

    # 查找可用的中文字体
    chinese_fonts = [
        f.name
        for f in fm.fontManager.ttflist
        if "SimHei" in f.name
        or "WenQuanYi" in f.name
        or "Noto Sans CJK" in f.name
        or "DejaVu" in f.name
    ]
    if chinese_fonts:
        plt.rcParams["font.sans-serif"] = chinese_fonts[0]
        print(f"使用字体: {chinese_fonts[0]}")
    else:
        # 如果没有中文字体，使用英文标签
        plt.rcParams["font.sans-serif"] = ["DejaVu Sans"]
        print("未找到中文字体，使用英文标签")
except Exception as e:
    plt.rcParams["font.sans-serif"] = ["DejaVu Sans"]
    print(f"字体设置失败: {e}，使用英文标签")


def load_benchmark_results(file_path):
    """加载基准测试结果"""
    with open(file_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data["results"]


def extract_build_data(results):
    """提取构建性能数据"""
    build_data = {}
    for result in results:
        if result["operation"] == "build" and result["index_type"] == "s2":
            size = result["data_size"]
            avg_time = result["avg_time_ms"]
            if avg_time > 0:
                build_data[size] = avg_time
    return build_data


def extract_query_data(results):
    """提取查询性能数据"""
    s2_query_data = {}
    ogr_query_data = {}

    for result in results:
        if result["operation"] == "spatial_query":
            size = result["data_size"]
            avg_time = result["avg_time_ms"]
            if avg_time > 0:
                if result["index_type"] == "s2":
                    s2_query_data[size] = avg_time
                elif result["index_type"] == "ogr":
                    ogr_query_data[size] = avg_time

    return s2_query_data, ogr_query_data


def plot_build_performance():
    """绘制构建性能图表 - 柱形图形式"""
    # 加载所有数据集的结果
    datasets = {
        "十万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_522730_filegdb_fixed.json",
        "百万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_532300_filegdb_fixed.json",
        "千万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_530000_filegdb_clean.json",
    }

    fig, ax = plt.subplots(figsize=(18, 12))  # 增加图表尺寸给文字更多空间

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]

    # 收集所有数据用于柱形图
    all_data = {}
    x_labels = []

    for i, (name, file_path) in enumerate(datasets.items()):
        print(f"处理数据集: {name}")
        results = load_benchmark_results(file_path)
        build_data = extract_build_data(results)

        sizes = sorted(build_data.keys())
        times = [build_data[size] for size in sizes]

        # 转换为秒
        times_sec = [t / 1000.0 for t in times]

        print(f"  {name}: {len(sizes)} 个数据点")
        print(f"  Sizes: {sizes}")
        print(f"  Times (sec): {[f'{t:.3f}' for t in times_sec]}")

        all_data[name] = {"sizes": sizes, "times_sec": times_sec, "color": colors[i]}

        # 收集所有规模标签
        for size in sizes:
            if size not in x_labels:
                x_labels.append(size)

    # 排序x轴标签
    x_labels = sorted(x_labels)

    # 设置柱形图参数 - 增加间距避免重叠
    bar_width = 0.2  # 减小柱子宽度
    x_pos = np.arange(len(x_labels))
    spacing = 0.1  # 增加柱子之间的间距

    # 绘制柱形图
    for i, (name, data) in enumerate(all_data.items()):
        # 为每个数据集创建对应的y值数组
        y_values = []
        for size in x_labels:
            if size in data["sizes"]:
                idx = data["sizes"].index(size)
                y_values.append(data["times_sec"][idx])
            else:
                y_values.append(0)  # 如果该规模不存在，设为0

        # 绘制柱形图 - 使用间距避免重叠
        bars = ax.bar(
            x_pos + i * (bar_width + spacing),
            y_values,
            bar_width,
            label=f"{name}数据集",
            color=data["color"],
            alpha=0.8,
            edgecolor="white",
            linewidth=1.5,
        )

        # 在柱子上添加数值标签 - 统一水平方向，通过位置避免重叠
        for j, (bar, value) in enumerate(zip(bars, y_values)):
            if value > 0:  # 只标注有数据的柱子
                height = bar.get_height()

                # 统一使用水平文字，所有标签都放在柱形顶部
                # 根据数值大小调整字体大小和偏移量
                if height < 0.02:  # 很小的数值
                    # 将标签放在柱子顶部，使用小字体
                    y_pos = height + height * 0.1  # 较大偏移量
                    color = "black"
                    fontsize = 7
                elif height < 0.1:  # 较小的数值
                    # 将标签放在柱子顶部，使用较小字体
                    y_pos = height + height * 0.05  # 中等偏移量
                    color = "black"
                    fontsize = 8
                else:  # 较大的数值
                    # 将标签放在柱子顶部，使用正常字体
                    y_pos = height + height * 0.02  # 较小偏移量
                    color = "black"
                    fontsize = 9

                ax.text(
                    bar.get_x() + bar.get_width() / 2.0,
                    y_pos,
                    f"{value:.3f}s",
                    ha="center",
                    va="bottom",
                    fontsize=fontsize,
                    fontweight="bold",
                    color=color,
                    rotation=0,  # 统一不旋转
                )

    # 设置图表属性
    ax.set_xlabel("数据规模 (要素数)", fontsize=16, fontweight="bold")
    ax.set_ylabel("构建时间 (秒)", fontsize=16, fontweight="bold")
    ax.set_title("S2索引构建性能分析", fontsize=18, fontweight="bold")
    ax.legend(fontsize=14, loc="upper left")
    ax.grid(True, alpha=0.3, linestyle="--", axis="y")
    ax.set_yscale("log")
    ax.tick_params(axis="both", which="major", labelsize=12)

    # 设置x轴刻度和标签 - 每个规模只显示一次，位置在三个柱子的中心
    center_positions = x_pos + (bar_width + spacing) * (len(all_data) - 1) / 2
    ax.set_xticks(center_positions)

    # 生成更清晰的刻度标签，避免重复
    def format_size_label(size):
        if size in [100, 1000, 10000, 100000, 1000000, 10000000]:
            return f"10^{int(np.log10(size))}"
        else:
            # 对于500, 5000, 50000, 500000, 5000000这样的值
            base = 10 ** int(np.log10(size))
            multiplier = size // base
            return f"{multiplier}×10^{int(np.log10(base))}"

    ax.set_xticklabels([format_size_label(x) for x in x_labels], rotation=45)

    plt.tight_layout()
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图1_修正索引构建性能.png",
        dpi=300,
        bbox_inches="tight",
    )
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图1_修正索引构建性能.pdf",
        bbox_inches="tight",
    )
    plt.close()  # 关闭图形以释放内存
    print("图1已保存")


def plot_query_performance():
    """绘制查询性能对比图表"""
    # 加载所有数据集的结果
    datasets = {
        "十万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_522730_filegdb_fixed.json",
        "百万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_532300_filegdb_fixed.json",
        "千万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_530000_filegdb_clean.json",
    }

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 8))

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]
    markers = ["o", "s", "^"]

    for i, (name, file_path) in enumerate(datasets.items()):
        print(f"处理查询数据集: {name}")
        results = load_benchmark_results(file_path)
        s2_data, ogr_data = extract_query_data(results)

        # 找到共同的数据规模
        common_sizes = sorted(set(s2_data.keys()) & set(ogr_data.keys()))
        print(f"  {name}: {len(common_sizes)} 个共同数据点")

        if len(common_sizes) > 0:
            s2_times = [s2_data[size] for size in common_sizes]
            ogr_times = [ogr_data[size] for size in common_sizes]

            # 绘制查询时间对比
            ax1.plot(
                common_sizes,
                s2_times,
                marker=markers[i],
                linestyle="-",
                label=f"{name}数据集-S2",
                color=colors[i],
                linewidth=2.5,
                markersize=8,
                markerfacecolor=colors[i],
                markeredgecolor="white",
                markeredgewidth=1.5,
                alpha=0.8,
            )

            ax1.plot(
                common_sizes,
                ogr_times,
                marker=markers[i],
                linestyle="--",
                label=f"{name}数据集-OGR",
                color=colors[i],
                linewidth=2.5,
                markersize=8,
                markerfacecolor="white",
                markeredgecolor=colors[i],
                markeredgewidth=1.5,
                alpha=0.6,
            )

            # 计算性能提升倍数
            speedups = [
                ogr_times[j] / s2_times[j] if s2_times[j] > 0 else float("inf")
                for j in range(len(common_sizes))
            ]

            # 处理无穷大值
            speedups_plot = [
                min(speedup, 10000) if speedup != float("inf") else 10000
                for speedup in speedups
            ]

            ax2.plot(
                common_sizes,
                speedups_plot,
                marker=markers[i],
                linestyle="-",
                label=f"{name}数据集",
                color=colors[i],
                linewidth=2.5,
                markersize=8,
                markerfacecolor=colors[i],
                markeredgecolor="white",
                markeredgewidth=1.5,
                alpha=0.8,
            )

    # 设置图表属性
    ax1.set_xlabel("数据规模 (要素数)", fontsize=16, fontweight="bold")
    ax1.set_ylabel("查询时间 (毫秒)", fontsize=16, fontweight="bold")
    ax1.set_title("S2 vs OGR 查询时间对比", fontsize=18, fontweight="bold")
    ax1.legend(fontsize=12, loc="upper left")
    ax1.grid(True, alpha=0.3, linestyle="--")
    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.tick_params(axis="both", which="major", labelsize=12)

    ax2.set_xlabel("数据规模 (要素数)", fontsize=16, fontweight="bold")
    ax2.set_ylabel("性能提升倍数", fontsize=16, fontweight="bold")
    ax2.set_title("S2索引性能提升分析", fontsize=18, fontweight="bold")
    ax2.legend(fontsize=12, loc="upper left")
    ax2.grid(True, alpha=0.3, linestyle="--")
    ax2.set_xscale("log")
    ax2.set_yscale("log")
    ax2.tick_params(axis="both", which="major", labelsize=12)

    # 设置x轴刻度
    x_ticks = [100, 1000, 10000, 100000, 1000000, 10000000, 16000000]
    ax1.set_xticks(x_ticks)
    ax1.set_xticklabels([f"10^{int(np.log10(x))}" for x in x_ticks])
    ax2.set_xticks(x_ticks)
    ax2.set_xticklabels([f"10^{int(np.log10(x))}" for x in x_ticks])

    plt.tight_layout()
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图2_修正查询性能对比.png",
        dpi=300,
        bbox_inches="tight",
    )
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图2_修正查询性能对比.pdf",
        bbox_inches="tight",
    )
    plt.close()
    print("图2已保存")


def plot_scalability_analysis():
    """绘制可扩展性分析图表"""
    # 加载所有数据集的结果
    datasets = {
        "十万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_522730_filegdb_fixed.json",
        "百万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_532300_filegdb_fixed.json",
        "千万级": "/home/chenming/Projects/test/s2-test/cpp/build/corrected_benchmark_530000_filegdb_clean.json",
    }

    fig, ax = plt.subplots(figsize=(14, 10))

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]
    markers = ["o", "s", "^"]

    for i, (name, file_path) in enumerate(datasets.items()):
        print(f"处理可扩展性数据集: {name}")
        results = load_benchmark_results(file_path)
        build_data = extract_build_data(results)

        sizes = sorted(build_data.keys())
        times = [build_data[size] for size in sizes]

        # 转换为秒
        times_sec = [t / 1000.0 for t in times]

        print(f"  {name}: {len(sizes)} 个数据点")
        print(f"  Sizes: {sizes}")
        print(f"  Times (sec): {[f'{t:.3f}' for t in times_sec]}")

        # 绘制散点图
        ax.scatter(
            sizes,
            times_sec,
            marker=markers[i],
            label=f"{name}数据集",
            color=colors[i],
            s=100,
            alpha=0.8,
            edgecolors="white",
            linewidth=2,
        )

        # 进行幂律拟合
        if len(sizes) > 2:
            from scipy import stats

            log_sizes = np.log(sizes)
            log_times = np.log(times_sec)

            # 线性拟合
            slope, intercept, r_value, p_value, std_err = stats.linregress(
                log_sizes, log_times
            )

            # 绘制拟合线
            fit_times = np.exp(intercept) * (np.array(sizes) ** slope)
            ax.plot(
                sizes,
                fit_times,
                color=colors[i],
                linestyle="--",
                alpha=0.7,
                linewidth=2,
                label=f"{name}拟合 (O(n^{slope:.3f}), R²={r_value**2:.3f})",
            )

    # 设置图表属性
    ax.set_xlabel("数据规模 (要素数)", fontsize=16, fontweight="bold")
    ax.set_ylabel("构建时间 (秒)", fontsize=16, fontweight="bold")
    ax.set_title("S2索引可扩展性分析", fontsize=18, fontweight="bold")
    ax.legend(fontsize=12, loc="upper left")
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.tick_params(axis="both", which="major", labelsize=12)

    # 设置x轴刻度
    x_ticks = [100, 1000, 10000, 100000, 1000000, 10000000, 16000000]
    ax.set_xticks(x_ticks)
    ax.set_xticklabels([f"10^{int(np.log10(x))}" for x in x_ticks])

    plt.tight_layout()
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图3_修正可扩展性分析.png",
        dpi=300,
        bbox_inches="tight",
    )
    plt.savefig(
        "/home/chenming/Projects/test/s2-test/academic_paper_figures/图3_修正可扩展性分析.pdf",
        bbox_inches="tight",
    )
    plt.close()
    print("图3已保存")


def main():
    """主函数"""
    print("开始生成论文图表...")

    print("生成构建性能图表...")
    plot_build_performance()

    print("生成查询性能对比图表...")
    plot_query_performance()

    print("生成可扩展性分析图表...")
    plot_scalability_analysis()

    print("所有图表生成完成！")
    print("图表保存在: /home/chenming/Projects/test/s2-test/academic_paper_figures/")


if __name__ == "__main__":
    main()
