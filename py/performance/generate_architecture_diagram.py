#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分离式存储架构图生成器 - 学术论文标准版本
参考SLAM架构图风格，符合学术论文标准
"""

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle, Arrow
import numpy as np
import warnings
from matplotlib import font_manager
import os

# 忽略字体警告
warnings.filterwarnings("ignore", category=UserWarning, module="matplotlib")


def setup_chinese_fonts():
    """设置中文字体，强制使用可用的中文字体"""
    import matplotlib.font_manager as fm

    # 中文字体优先级列表
    chinese_fonts = [
        "WenQuanYi Micro Hei",  # 文泉驿微米黑
        "WenQuanYi Zen Hei",  # 文泉驿正黑
        "Noto Sans CJK SC",  # Google Noto 中文字体
        "AR PL UMing CN",  # 文鼎PL中楷
        "AR PL UKai CN",  # 文鼎PL中楷
        "SimHei",  # 黑体
        "Microsoft YaHei",  # 微软雅黑
    ]

    # 获取系统中可用的字体
    available_fonts = [f.name for f in fm.fontManager.ttflist]

    # 找到第一个可用的中文字体
    selected_font = None
    for font in chinese_fonts:
        if font in available_fonts:
            selected_font = font
            break

    if selected_font:
        print(f"使用中文字体: {selected_font}")
        plt.rcParams["font.sans-serif"] = [selected_font]
        plt.rcParams["font.family"] = "sans-serif"
        plt.rcParams["axes.unicode_minus"] = False

        # 设置字体属性
        plt.rcParams["font.size"] = 10
        plt.rcParams["font.weight"] = "normal"

        return selected_font
    else:
        print("警告: 未找到合适的中文字体，使用默认字体")
        plt.rcParams["font.sans-serif"] = ["DejaVu Sans"]
        plt.rcParams["font.family"] = "sans-serif"
        plt.rcParams["axes.unicode_minus"] = False
        return "DejaVu Sans"


# 设置中文字体
selected_font = setup_chinese_fonts()

# 设置matplotlib参数
plt.rcParams["figure.dpi"] = 300
plt.rcParams["savefig.dpi"] = 300
plt.rcParams["axes.linewidth"] = 0.8
plt.rcParams["grid.linewidth"] = 0.5


def create_architecture_diagram():
    """创建分离式存储架构图 - 学术论文标准版本"""

    # 创建图形 - 学术论文标准尺寸
    fig, ax = plt.subplots(1, 1, figsize=(12, 8))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 8)
    ax.axis("off")

    # 学术论文标准配色方案 - 参考SLAM架构图
    colors = {
        "box": "#E3F2FD",  # 统一浅蓝色框体 - 参考SLAM风格
        "text": "#2C3E50",  # 深蓝灰色文字
        "border": "#34495E",  # 深灰色边框
        "section_border": "#7F8C8D",  # 虚线区域边框
        "arrow": "#2C3E50",  # 箭头和连接线颜色
    }

    # 主标题
    ax.text(
        6,
        7.5,
        "分离式存储架构设计",
        fontsize=20,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 第一区域：输入数据源 - 参考SLAM的"Input Sensor Data"区域
    input_section = FancyBboxPatch(
        (0.5, 6.2),
        3.5,
        1.0,
        boxstyle="round,pad=0.1",
        facecolor="none",
        edgecolor=colors["section_border"],
        linewidth=1.5,
        linestyle="--",
    )
    ax.add_patch(input_section)
    ax.text(
        2.25,
        7.0,
        "输入数据源",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 输入数据框 - 参考SLAM的框体样式
    shapefile_box = FancyBboxPatch(
        (0.8, 6.4),
        1.2,
        0.4,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(shapefile_box)
    ax.text(
        1.4,
        6.6,
        "Shapefile",
        fontsize=16,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    filegdb_box = FancyBboxPatch(
        (2.2, 6.4),
        1.2,
        0.4,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(filegdb_box)
    ax.text(
        2.8,
        6.6,
        "FileGDB",
        fontsize=16,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 第二区域：数据转换处理 - 参考SLAM的中间处理模块
    converter_box = FancyBboxPatch(
        (4.5, 6.4),
        2.0,
        0.4,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(converter_box)
    ax.text(
        5.5,
        6.6,
        "OGR格式转换器",
        fontsize=16,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 第三区域：分离式存储层 - 参考SLAM的"Local SLAM"区域
    storage_section = FancyBboxPatch(
        (0.5, 4.5),
        11,
        1.2,
        boxstyle="round,pad=0.1",
        facecolor="none",
        edgecolor=colors["section_border"],
        linewidth=1.5,
        linestyle="--",
    )
    ax.add_patch(storage_section)
    ax.text(
        6,
        5.5,
        "分离式存储层",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 几何数据存储
    geom_box = FancyBboxPatch(
        (0.8, 4.7),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(geom_box)
    ax.text(
        2.05,
        5.0,
        "几何数据存储",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        2.05,
        4.8,
        ".geom",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 属性数据存储
    attr_box = FancyBboxPatch(
        (3.8, 4.7),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(attr_box)
    ax.text(
        5.05,
        5.0,
        "属性数据存储",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        5.05,
        4.8,
        ".attr",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 字符串池存储
    pool_box = FancyBboxPatch(
        (6.8, 4.7),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(pool_box)
    ax.text(
        8.05,
        5.0,
        "字符串池存储",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        8.05,
        4.8,
        ".pool",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 元数据
    meta_box = FancyBboxPatch(
        (9.8, 4.7),
        1.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(meta_box)
    ax.text(
        10.55,
        5.0,
        "元数据",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        10.55,
        4.8,
        "_meta.json",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 第四区域：索引层 - 参考SLAM的"Global SLAM"区域
    index_section = FancyBboxPatch(
        (0.5, 2.8),
        11,
        1.2,
        boxstyle="round,pad=0.1",
        facecolor="none",
        edgecolor=colors["section_border"],
        linewidth=1.5,
        linestyle="--",
    )
    ax.add_patch(index_section)
    ax.text(
        6,
        3.8,
        "索引层",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 几何数据分块索引 - 与几何数据存储对齐
    geom_chunked_box = FancyBboxPatch(
        (0.8, 3.0),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(geom_chunked_box)
    ax.text(
        2.05,
        3.3,
        "几何分块索引",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        2.05,
        3.1,
        ".geom.chunked_idx",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 属性数据分块索引 - 与属性数据存储对齐
    attr_chunked_box = FancyBboxPatch(
        (3.8, 3.0),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(attr_chunked_box)
    ax.text(
        5.05,
        3.3,
        "属性分块索引",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        5.05,
        3.1,
        ".attr.chunked_idx",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # S2空间索引 - 与字符串池存储对齐
    s2_box = FancyBboxPatch(
        (6.8, 3.0),
        2.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(s2_box)
    ax.text(
        8.05,
        3.3,
        "S2空间索引",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        8.05,
        3.1,
        ".s2idx",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 字符串池索引 - 与元数据对齐
    pool_index_box = FancyBboxPatch(
        (9.8, 3.0),
        1.5,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(pool_index_box)
    ax.text(
        10.55,
        3.3,
        "字符串池索引",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        10.55,
        3.1,
        ".pool.index",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 第五区域：查询层
    query_section = FancyBboxPatch(
        (0.5, 1.0),
        11,
        1.2,
        boxstyle="round,pad=0.1",
        facecolor="none",
        edgecolor=colors["section_border"],
        linewidth=1.5,
        linestyle="--",
    )
    ax.add_patch(query_section)
    ax.text(
        6,
        1.9,
        "查询层",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 统一查询接口
    query_box = FancyBboxPatch(
        (4.5, 1.2),
        3,
        0.6,
        boxstyle="round,pad=0.02",
        facecolor=colors["box"],
        edgecolor=colors["border"],
        linewidth=1.0,
    )
    ax.add_patch(query_box)
    ax.text(
        6,
        1.5,
        "统一查询接口",
        fontsize=16,
        fontweight="bold",
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )
    ax.text(
        6,
        1.3,
        "空间查询 | 属性查询 | 混合查询",
        fontsize=14,
        ha="center",
        color=colors["text"],
        fontfamily=selected_font,
    )

    # 连接线 - 使用折线连接，多个目标先汇合再连接
    # 输入到转换器 - 使用折线连接，避免与实体重叠
    # Shapefile -> 汇合点
    ax.plot([1.4, 1.4], [6.6, 7.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向上延长
    ax.plot([1.4, 3.5], [7.0, 7.0], color=colors["arrow"],
            linewidth=1.2)  # 水平到汇合点

    # FileGDB -> 汇合点
    ax.plot([2.8, 2.8], [6.6, 7.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向上延长

    # 从汇合点到转换器
    ax.plot([3.5, 3.5], [7.0, 6.6], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([3.5, 4.5], [6.6, 6.6], color=colors["arrow"],
            linewidth=1.2)  # 水平到转换器
    ax.arrow(
        4.3,
        6.6,
        0.15,
        0,
        head_width=0.03,
        head_length=0.05,
        fc=colors["arrow"],
        ec=colors["arrow"],
        linewidth=1.2,
    )

    # 转换器到存储层 - 垂直连接
    ax.plot([5.5, 5.5], [6.4, 5.3], color=colors["arrow"], linewidth=1.2)
    ax.arrow(
        5.5,
        5.35,
        0,
        -0.05,
        head_width=0.05,
        head_length=0.02,
        fc=colors["arrow"],
        ec=colors["arrow"],
        linewidth=1.2,
    )

    # 存储层到索引层 - 使用折线连接，先汇合到中心点
    # 几何数据存储 -> 汇合点
    ax.plot([2.05, 2.05], [4.7, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([2.05, 5.5], [4.0, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 属性数据存储 -> 汇合点
    ax.plot([5.05, 5.05], [4.7, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下

    # 字符串池存储 -> 汇合点
    ax.plot([8.05, 8.05], [4.7, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([8.05, 5.5], [4.0, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 元数据 -> 汇合点
    ax.plot([10.55, 10.55], [4.7, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([10.55, 5.5], [4.0, 4.0], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 从汇合点到索引层
    ax.plot([5.5, 5.5], [4.0, 3.6], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.arrow(
        5.5,
        3.65,
        0,
        -0.05,
        head_width=0.05,
        head_length=0.02,
        fc=colors["arrow"],
        ec=colors["arrow"],
        linewidth=1.2,
    )

    # 索引层到查询层 - 使用折线连接，先汇合到中心点
    # 几何分块索引 -> 汇合点
    ax.plot([2.05, 2.05], [3.0, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([2.05, 6], [2.5, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 属性分块索引 -> 汇合点
    ax.plot([5.05, 5.05], [3.0, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([5.05, 6], [2.5, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # S2空间索引 -> 汇合点
    ax.plot([8.05, 8.05], [3.0, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([8.05, 6], [2.5, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 字符串池索引 -> 汇合点
    ax.plot([10.55, 10.55], [3.0, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 垂直向下
    ax.plot([10.55, 6], [2.5, 2.5], color=colors["arrow"],
            linewidth=1.2)  # 水平到中心

    # 从汇合点到查询层
    ax.plot([6, 6], [2.5, 1.8], color=colors["arrow"], linewidth=1.2)  # 垂直向下
    ax.arrow(
        6,
        1.85,
        0,
        -0.05,
        head_width=0.05,
        head_length=0.02,
        fc=colors["arrow"],
        ec=colors["arrow"],
        linewidth=1.2,
    )

    plt.tight_layout()
    return fig


def main():
    """主函数"""
    print("正在生成分离式存储架构图（学术论文标准版本）...")

    # 创建架构图
    fig1 = create_architecture_diagram()
    fig1.savefig(
        "fig3_1_architecture_diagram.pdf",
        format="pdf",
        bbox_inches="tight",
        dpi=300,
        facecolor="white",
        edgecolor="none",
    )
    fig1.savefig(
        "fig3_1_architecture_diagram.png",
        format="png",
        bbox_inches="tight",
        dpi=300,
        facecolor="white",
        edgecolor="none",
    )
    plt.close(fig1)

    print("图表生成完成！")
    print("生成的文件：")
    print("- fig3_1_architecture_diagram.pdf/png")


if __name__ == "__main__":
    main()
