#!/bin/bash

# GIS存储系统测试运行脚本

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# 显示帮助
show_help() {
    echo "GIS存储系统测试运行脚本"
    echo ""
    echo "用法: $0 [选项] [测试名称]"
    echo ""
    echo "选项:"
    echo "  -h, --help      显示此帮助信息"
    echo "  -u, --unit      运行单元测试"ppp
    echo "  -i, --integration 运行集成测试"
    echo "  -p, --performance 运行性能测试"
    echo "  -a, --all       运行所有测试"
    echo "  -v, --verbose   详细输出"
    echo "  -c, --coverage  生成覆盖率报告"
    echo "  -q, --quick     快速测试（跳过耗时测试）"
    echo ""
    echo "测试名称:"
    echo "  s2index_test              S2索引单元测试"
    echo "  gis_storage_test          GIS存储单元测试"
    echo "  integrated_gis_format_test 集成测试"
    echo "  file_io_performance_test  性能测试"
    echo ""
    echo "示例:"
    echo "  $0 --all                  # 运行所有测试"
    echo "  $0 --unit                 # 运行单元测试"
    echo "  $0 s2index_test           # 运行特定测试"
    echo ""
}

# 默认参数
RUN_UNIT=false
RUN_INTEGRATION=false
RUN_PERFORMANCE=false
RUN_ALL=false
VERBOSE=false
COVERAGE=false
QUICK_MODE=false
TEST_NAME=""

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -u|--unit)
            RUN_UNIT=true
            shift
            ;;
        -i|--integration)
            RUN_INTEGRATION=true
            shift
            ;;
        -p|--performance)
            RUN_PERFORMANCE=true
            shift
            ;;
        -a|--all)
            RUN_ALL=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -c|--coverage)
            COVERAGE=true
            shift
            ;;
        -q|--quick)
            QUICK_MODE=true
            shift
            ;;
        -*)
            print_error "未知选项: $1"
            show_help
            exit 1
            ;;
        *)
            TEST_NAME="$1"
            shift
            ;;
    esac
done

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# 查找build目录（可能在项目根目录或cpp目录下）
if [ -d "$PROJECT_ROOT/build" ]; then
    BUILD_DIR="$PROJECT_ROOT/build"
elif [ -d "$PROJECT_ROOT/../build" ]; then
    BUILD_DIR="$PROJECT_ROOT/../build"
else
    BUILD_DIR="$PROJECT_ROOT/build"
fi

print_info "项目根目录: $PROJECT_ROOT"
print_info "构建目录: $BUILD_DIR"

# 检查构建目录
if [ ! -d "$BUILD_DIR" ]; then
    print_error "构建目录不存在，请先运行构建脚本"
    exit 1
fi

cd "$BUILD_DIR/tests"

# 设置测试选项
GTEST_OPTIONS=""
if [ "$VERBOSE" = true ]; then
    GTEST_OPTIONS="--gtest_output=xml:test_results.xml"
fi

# 运行特定测试
if [ -n "$TEST_NAME" ]; then
    print_info "运行测试: $TEST_NAME"
    
    if [ -f "$TEST_NAME" ]; then
        if [ "$VERBOSE" = true ]; then
            ./"$TEST_NAME" $GTEST_OPTIONS
        else
            ./"$TEST_NAME"
        fi
        
        if [ $? -eq 0 ]; then
            print_success "测试 $TEST_NAME 通过"
        else
            print_error "测试 $TEST_NAME 失败"
            exit 1
        fi
    else
        print_error "测试文件不存在: $TEST_NAME"
        exit 1
    fi
    exit 0
fi

# 运行测试组
TESTS_PASSED=0
TESTS_FAILED=0

run_test() {
    local test_name="$1"
    local test_file="$2"
    local timeout_seconds="${3:-300}"  # 默认5分钟超时
    
    if [ -f "$test_file" ]; then
        print_info "运行测试: $test_name (超时: ${timeout_seconds}秒)"
        
        if [ "$VERBOSE" = true ]; then
            timeout "$timeout_seconds" ./"$test_file" $GTEST_OPTIONS
        else
            timeout "$timeout_seconds" ./"$test_file" > /dev/null 2>&1
        fi
        
        local exit_code=$?
        if [ $exit_code -eq 0 ]; then
            print_success "✓ $test_name 通过"
            ((TESTS_PASSED++))
        elif [ $exit_code -eq 124 ]; then
            print_error "✗ $test_name 超时 (${timeout_seconds}秒)"
            ((TESTS_FAILED++))
        else
            print_error "✗ $test_name 失败 (退出码: $exit_code)"
            ((TESTS_FAILED++))
        fi
    else
        print_warning "测试文件不存在: $test_file"
    fi
}

# 运行单元测试
if [ "$RUN_UNIT" = true ] || [ "$RUN_ALL" = true ]; then
    print_info "=== 运行单元测试 ==="
    if [ "$QUICK_MODE" = true ]; then
        print_info "快速模式：跳过S2索引测试（包含大数据集测试）"
        run_test "GIS存储测试" "gis_storage_test" 300  # 5分钟超时
    else
        run_test "S2索引测试" "s2index_test" 600  # 10分钟超时，因为包含大数据集测试
        run_test "GIS存储测试" "gis_storage_test" 300  # 5分钟超时
    fi
fi

# 运行集成测试
if [ "$RUN_INTEGRATION" = true ] || [ "$RUN_ALL" = true ]; then
    print_info "=== 运行集成测试 ==="
    if [ "$QUICK_MODE" = true ]; then
        print_info "快速模式：跳过集成测试（可能耗时较长）"
    else
        run_test "集成格式测试" "integrated_gis_format_test" 600  # 10分钟超时
    fi
fi

# 运行性能测试
if [ "$RUN_PERFORMANCE" = true ] || [ "$RUN_ALL" = true ]; then
    print_info "=== 运行性能测试 ==="
    run_test "文件I/O性能测试" "file_io_performance_test" 300  # 5分钟超时
fi

# 如果没有指定任何选项，运行所有测试
if [ "$RUN_UNIT" = false ] && [ "$RUN_INTEGRATION" = false ] && [ "$RUN_PERFORMANCE" = false ] && [ "$RUN_ALL" = false ]; then
    print_info "=== 运行所有测试 ==="
    if [ "$QUICK_MODE" = true ]; then
        print_info "快速模式：只运行基础测试"
        run_test "GIS存储测试" "gis_storage_test" 300
        run_test "文件I/O性能测试" "file_io_performance_test" 300
    else
        run_test "S2索引测试" "s2index_test" 600
        run_test "GIS存储测试" "gis_storage_test" 300
        run_test "集成格式测试" "integrated_gis_format_test" 600
        run_test "文件I/O性能测试" "file_io_performance_test" 300
    fi
fi

# 显示测试结果
echo ""
print_info "=== 测试结果汇总 ==="
print_success "通过: $TESTS_PASSED"
if [ $TESTS_FAILED -gt 0 ]; then
    print_error "失败: $TESTS_FAILED"
    exit 1
else
    print_success "失败: $TESTS_FAILED"
fi

# 生成覆盖率报告
if [ "$COVERAGE" = true ]; then
    print_info "生成覆盖率报告..."
    # 这里可以添加覆盖率报告生成逻辑
    print_warning "覆盖率报告功能待实现"
fi

print_success "所有测试完成！"
