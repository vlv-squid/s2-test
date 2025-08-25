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
    
    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake未安装，请先安装CMake"
        exit 1
    fi
    
    # 检查make
    if ! command -v make &> /dev/null; then
        print_error "make未安装，请先安装make"
        exit 1
    fi
    
    # 检查Python
    if ! command -v python3 &> /dev/null; then
        print_warning "Python3未安装，将跳过可视化步骤"
        SKIP_VISUALIZATION=true
    fi
    
    print_success "依赖检查完成"
}

# 编译项目
build_project() {
    print_info "编译C++项目..."
    
    cd ../..
    
    # 创建构建目录
    if [ ! -d "cpp/build" ]; then
        mkdir -p cpp/build
    fi
    
    cd cpp/build
    
    # 配置CMake
    print_info "配置CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release
    
    # 编译
    print_info "编译项目..."
    make -j$(nproc)
    
    # 检查编译结果
    if [ ! -f "spatial_index_benchmark" ]; then
        print_error "编译失败，spatial_index_benchmark未生成"
        exit 1
    fi
    
    print_success "编译完成"
    cd ../scripts
}

# 运行benchmark
run_benchmark() {
    local data_path=$1
    local output_file=$2
    
    print_info "运行空间索引性能测试..."
    print_info "数据路径: $data_path"
    print_info "输出文件: $output_file"
    
    # 检查数据文件是否存在
    if [ ! -f "$data_path" ] && [ ! -d "$data_path" ]; then
        print_error "数据文件不存在: $data_path"
        exit 1
    fi
    
    # 运行benchmark
    cd ../build
    ./spatial_index_benchmark "$data_path" "$output_file"
    cd ../scripts
    
    # 检查输出文件
    if [ ! -f "../build/$output_file" ]; then
        print_error "Benchmark输出文件未生成: ../build/$output_file"
        exit 1
    fi
    
    print_success "Benchmark测试完成"
}

# 生成可视化图表
generate_visualization() {
    local json_file=$1
    local output_dir=$2
    
    if [ "$SKIP_VISUALIZATION" = true ]; then
        print_warning "跳过可视化步骤（Python未安装）"
        return
    fi
    
    print_info "生成可视化图表..."
    
    # 检查Python依赖
    python3 -c "import matplotlib, numpy, pandas" 2>/dev/null || {
        print_warning "Python依赖不完整，尝试安装..."
        pip3 install matplotlib numpy pandas || {
            print_warning "无法安装Python依赖，跳过可视化"
            return
        }
    }
    
    # 创建输出目录
    mkdir -p "$output_dir"
    
    # 运行可视化脚本
    python3 ../../py/performance/cpp_benchmark_visualizer.py "../build/$json_file" \
        --output-dir "$output_dir" \
        --detailed \
        --summary
    
    print_success "可视化图表生成完成"
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
    echo "  --skip-build            跳过编译步骤"
    echo "  --skip-visualization    跳过可视化步骤"
    echo ""
    echo "示例:"
    echo "  $0 data/test.gdb"
    echo "  $0 data/test.shp -o results.json -d results"
    echo "  $0 data/test.gdb --skip-build"
    echo ""
    echo "支持的数据格式:"
    echo "  - GeoDatabase (.gdb)"
    echo "  - Shapefile (.shp)"
    echo "  - 其他GDAL/OGR支持的格式"
}

# 主函数
main() {
    local data_path=""
    local output_file="./benchmark_results.json"
    local output_dir="./benchmark_results"
    local skip_build=false
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
            --skip-build)
                skip_build=true
                shift
                ;;
            --skip-visualization)
                skip_visualization=true
                SKIP_VISUALIZATION=true
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
        print_error "请指定数据路径"
        show_help
        exit 1
    fi
    
    print_info "开始运行C++空间索引性能测试..."
    print_info "数据路径: $data_path"
    print_info "输出文件: $output_file"
    print_info "输出目录: $output_dir"
    
    # 检查依赖
    check_dependencies
    
    # 编译项目
    if [ "$skip_build" = false ]; then
        build_project
    else
        print_info "跳过编译步骤"
    fi
    
    # 运行benchmark
    run_benchmark "$data_path" "$output_file"
    
    # 生成可视化
    if [ "$skip_visualization" = false ]; then
        generate_visualization "$output_file" "$output_dir"
    else
        print_info "跳过可视化步骤"
    fi
    
    print_success "所有测试完成！"
    print_info "结果文件: $output_file"
    if [ "$skip_visualization" = false ] && [ "$SKIP_VISUALIZATION" != true ]; then
        print_info "可视化结果: $output_dir"
    fi
}

# 运行主函数
main "$@"
