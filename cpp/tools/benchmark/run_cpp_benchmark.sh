#!/bin/bash

# C++ Spatial Index Benchmark Runner
# 这个脚本用于运行C++空间索引性能测试

set -e

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

# 检查依赖
check_dependencies() {
    print_info "检查依赖..."
    
    # 获取脚本所在目录
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
    BUILD_DIR="$PROJECT_ROOT/build"
    BENCHMARK_EXECUTABLE="$BUILD_DIR/benchmarks/spatial_index_benchmark"
    
    # 检查基准测试可执行文件
    if [ ! -f "$BENCHMARK_EXECUTABLE" ]; then
        print_error "基准测试程序不存在: $BENCHMARK_EXECUTABLE"
        print_info "请先构建基准测试程序: ./tools/build/build.sh --benchmark"
        exit 1
    fi
    
    # 检查Python（用于可视化）
    if ! command -v python3 &> /dev/null; then
        print_warning "Python3未安装，将跳过可视化步骤"
        SKIP_VISUALIZATION=true
    else
        SKIP_VISUALIZATION=false
    fi
    
    print_success "依赖检查通过"
}

# 运行基准测试
run_benchmark() {
    local data_path="$1"
    local output_file="$2"
    
    print_info "运行基准测试..."
    print_info "数据路径: $data_path"
    print_info "输出文件: $output_file"
    
    # 切换到构建目录
    cd "$BUILD_DIR"
    
    # 调整数据路径（相对于build目录）
    local adjusted_data_path="$data_path"
    if [[ "$data_path" == ../* ]]; then
        # 如果路径以../开头，需要再添加一个../
        adjusted_data_path="../$data_path"
    fi
    
    # 运行基准测试
    if [ "$VERBOSE" = true ]; then
        ./benchmarks/spatial_index_benchmark "$adjusted_data_path" "$output_file"
        local exit_code=$?
    else
        ./benchmarks/spatial_index_benchmark "$adjusted_data_path" "$output_file"
        local exit_code=$?
    fi
    
    if [ $exit_code -eq 0 ]; then
        print_success "基准测试完成"
        if [ -f "$output_file" ]; then
            # 将结果文件复制到项目根目录
            local project_output_file="$PROJECT_ROOT/$(basename "$output_file")"
            cp "$output_file" "$project_output_file"
            print_info "结果已保存到: $project_output_file"
        fi
    else
        print_error "基准测试失败 (退出码: $exit_code)"
        exit 1
    fi
}

# 生成可视化报告
generate_visualization() {
    local json_file="$1"
    local output_dir="$2"
    
    if [ "$SKIP_VISUALIZATION" = true ]; then
        print_warning "跳过可视化步骤（Python3未安装）"
        return
    fi
    
    print_info "生成可视化报告..."
    
    # 检查可视化脚本是否存在
    local visualizer_script="$PROJECT_ROOT/../py/performance/cpp_benchmark_visualizer.py"
    if [ ! -f "$visualizer_script" ]; then
        print_warning "可视化脚本不存在: $visualizer_script"
        return
    fi
    
    # 运行可视化脚本
    if python3 "$visualizer_script" "$json_file" "$output_dir"; then
        print_success "可视化报告生成完成: $output_dir"
    else
        print_warning "可视化报告生成失败"
    fi
}

# 显示帮助信息
show_help() {
    echo "C++ Spatial Index Benchmark Runner"
    echo ""
    echo "用法: $0 [选项] <数据路径>"
    echo ""
    echo "选项:"
    echo "  -h, --help              显示此帮助信息"
    echo "  -o, --output FILE       指定输出JSON文件路径 (默认: ./benchmark_results.json)"
    echo "  -d, --output-dir DIR    指定可视化输出目录 (默认: ./benchmark_results)"
    echo "  -v, --verbose           详细输出"
    echo "  --skip-visualization    跳过可视化步骤"
    echo ""
    echo "示例:"
    echo "  $0 data/test.gdb"
    echo "  $0 data/test.shp -o results.json -d results"
    echo "  $0 data/test.gdb --skip-visualization"
    echo ""
    echo "支持的数据格式:"
    echo "  - GeoDatabase (.gdb)"
    echo "  - Shapefile (.shp)"
    echo "  - 其他GDAL/OGR支持的格式"
    echo ""
    echo "注意:"
    echo "  - 请确保已构建基准测试程序: ./tools/build/build.sh --benchmark"
    echo "  - 数据文件路径相对于项目根目录"
}

# 主函数
main() {
    # 默认参数
    local data_path=""
    local output_file="./benchmark_results.json"
    local output_dir="./benchmark_results"
    local verbose=false
    local skip_visualization=false
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -o|--output)
                output_file="$2"
                shift 2
                ;;
            -d|--output-dir)
                output_dir="$2"
                shift 2
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            --skip-visualization)
                skip_visualization=true
                shift
                ;;
            -*)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
            *)
                if [ -z "$data_path" ]; then
                    data_path="$1"
                else
                    print_error "只能指定一个数据路径"
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # 检查必需参数
    if [ -z "$data_path" ]; then
        print_error "缺少必需参数: 数据路径"
        show_help
        exit 1
    fi
    
    # 检查数据文件是否存在
    if [ ! -f "$data_path" ] && [ ! -d "$data_path" ]; then
        print_error "数据文件不存在: $data_path"
        exit 1
    fi
    
    # 设置全局变量
    VERBOSE="$verbose"
    if [ "$skip_visualization" = true ]; then
        SKIP_VISUALIZATION=true
    fi
    
    # 显示配置信息
    echo "=== C++空间索引基准测试 ==="
    echo "数据路径: $data_path"
    echo "输出文件: $output_file"
    echo "输出目录: $output_dir"
    echo "详细模式: $verbose"
    echo "跳过可视化: $skip_visualization"
    echo ""
    
    # 执行步骤
    check_dependencies
    run_benchmark "$data_path" "$output_file"
    
    if [ "$SKIP_VISUALIZATION" = false ]; then
        generate_visualization "$output_file" "$output_dir"
    fi
    
    print_success "基准测试完成！"
    print_info "结果文件: $output_file"
    if [ "$SKIP_VISUALIZATION" = false ]; then
        print_info "可视化报告: $output_dir"
    fi
}

# 运行主函数
main "$@"