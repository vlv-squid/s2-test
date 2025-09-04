#!/bin/bash

# GIS存储系统演示程序运行脚本

# 设置脚本选项
set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示帮助信息
show_help() {
    echo "GIS存储系统演示程序运行脚本"
    echo ""
    echo "用法: $0 [选项] <输入Shapefile> <输出目录>"
    echo ""
    echo "选项:"
    echo "  -h, --help     显示此帮助信息"
    echo "  -c, --clean    清理输出目录"
    echo "  -v, --verbose  详细输出"
    echo "  -t, --test     使用测试数据运行"
    echo ""
    echo "示例:"
    echo "  $0 ../data/test.shp ./output"
    echo "  $0 --test"
    echo "  $0 --clean ../data/test.shp ./output"
    echo ""
}

# 检查依赖
check_dependencies() {
    print_info "检查依赖项..."
    
    # 检查可执行文件是否存在
    if [ ! -f "./gis_storage_demo" ]; then
        print_error "演示程序不存在，请先构建程序"
        print_info "运行: cmake .. && make gis_storage_demo"
        exit 1
    fi
    
    # 检查输入文件（如果不是测试模式）
    if [ "$USE_TEST_DATA" != "true" ]; then
        if [ ! -f "$INPUT_FILE" ]; then
            print_error "输入文件不存在: $INPUT_FILE"
            exit 1
        fi
    fi
    
    print_success "依赖检查通过"
}

# 清理输出目录
clean_output() {
    if [ -d "$OUTPUT_DIR" ]; then
        print_info "清理输出目录: $OUTPUT_DIR"
        rm -rf "$OUTPUT_DIR"/*
        print_success "输出目录已清理"
    fi
}

# 运行演示程序
run_demo() {
    print_info "开始运行GIS存储系统演示程序..."
    echo ""
    
    if [ "$VERBOSE" = "true" ]; then
        # 详细模式
        ./gis_storage_demo "$INPUT_FILE" "$OUTPUT_DIR"
    else
        # 普通模式
        ./gis_storage_demo "$INPUT_FILE" "$OUTPUT_DIR" 2>&1 | tee demo_output.log
    fi
    
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        print_success "演示程序运行成功！"
        print_info "输出文件位于: $OUTPUT_DIR"
        
        # 显示生成的文件
        if [ -d "$OUTPUT_DIR" ]; then
            echo ""
            print_info "生成的文件:"
            ls -lh "$OUTPUT_DIR" | grep -v "^total" | while read line; do
                echo "  $line"
            done
        fi
        
        # 显示日志文件
        if [ -f "demo_output.log" ]; then
            print_info "详细日志保存在: demo_output.log"
        fi
        
    else
        print_error "演示程序运行失败 (退出码: $exit_code)"
        if [ -f "demo_output.log" ]; then
            print_info "错误日志:"
            tail -20 demo_output.log
        fi
        exit $exit_code
    fi
}

# 显示统计信息
show_stats() {
    if [ -d "$OUTPUT_DIR" ]; then
        echo ""
        print_info "文件统计信息:"
        
        # 计算总大小
        local total_size=$(du -sh "$OUTPUT_DIR" 2>/dev/null | cut -f1)
        echo "  总大小: $total_size"
        
        # 文件数量
        local file_count=$(find "$OUTPUT_DIR" -type f | wc -l)
        echo "  文件数量: $file_count"
        
        # 各文件大小
        echo "  文件详情:"
        find "$OUTPUT_DIR" -type f -exec ls -lh {} \; | while read line; do
            echo "    $line"
        done
    fi
}

# 主函数
main() {
    # 默认参数
    USE_TEST_DATA=false
    CLEAN_OUTPUT=false
    VERBOSE=false
    INPUT_FILE=""
    OUTPUT_DIR=""
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -c|--clean)
                CLEAN_OUTPUT=true
                shift
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -t|--test)
                USE_TEST_DATA=true
                INPUT_FILE="../data/test.shp"
                OUTPUT_DIR="./demo_output"
                shift
                ;;
            -*)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
            *)
                if [ -z "$INPUT_FILE" ]; then
                    INPUT_FILE="$1"
                elif [ -z "$OUTPUT_DIR" ]; then
                    OUTPUT_DIR="$1"
                else
                    print_error "参数过多"
                    show_help
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # 检查必需参数
    if [ "$USE_TEST_DATA" != "true" ]; then
        if [ -z "$INPUT_FILE" ] || [ -z "$OUTPUT_DIR" ]; then
            print_error "缺少必需参数"
            show_help
            exit 1
        fi
    fi
    
    # 显示配置信息
    echo "=== GIS存储系统演示程序 ==="
    echo "输入文件: $INPUT_FILE"
    echo "输出目录: $OUTPUT_DIR"
    echo "清理输出: $CLEAN_OUTPUT"
    echo "详细模式: $VERBOSE"
    echo "测试模式: $USE_TEST_DATA"
    echo ""
    
    # 执行步骤
    check_dependencies
    
    if [ "$CLEAN_OUTPUT" = "true" ]; then
        clean_output
    fi
    
    run_demo
    show_stats
    
    print_success "演示完成！"
}

# 运行主函数
main "$@"
