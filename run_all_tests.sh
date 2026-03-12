#!/bin/bash

################################################################################
# IPCSharedDataPool 单元测试启动脚本
#
# 功能：
#   - 清理旧的共享内存
#   - 编译所有测试
#   - 执行所有单元测试
#   - 生成测试报告
#
# 用法：
#   ./run_all_tests.sh [选项]
#
# 选项：
#   --clean         清理旧的共享内存并退出
#   --no-build      跳过编译，直接运行测试
#   --verbose       详细输出
#   --test NAME    只运行指定的测试
#   --list         列出所有可用的测试
#
################################################################################

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TEST_RESULTS_DIR="${BUILD_DIR}/test_results"

# 测试列表
TESTS=(
    "tst_common"
    "tst_rwlock"
    "tst_ringbuffer"
    "tst_shm_pool"
    "tst_event_center"
    "tst_integration"
    "tst_process_monitor"
)

# IEC61850 相关测试（需要 libiec61850）
IEC61850_TESTS=(
    "tst_log_service"
    "tst_adapters_basic"
)

# Qt 相关测试（需要 Qt5）
QT_TESTS=(
    "tst_soe_persist"
    "tst_voting_iec61850"
)

# 选项
CLEAN_ONLY=false
NO_BUILD=false
VERBOSE=false
SPECIFIC_TEST=""

# 打印帮助信息
print_help() {
    echo "IPCSharedDataPool 单元测试启动脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --clean         清理旧的共享内存并退出"
    echo "  --no-build      跳过编译，直接运行测试"
    echo "  --verbose       详细输出"
    echo "  --test NAME    只运行指定的测试"
    echo "  --list          列出所有可用的测试"
    echo "  --help          显示此帮助信息"
    echo ""
}

# 清理共享内存
clean_shm() {
    echo -e "${BLUE}清理共享内存...${NC}"

    # 清理 IPC 共享内存
    ipcrm -M 0x12345678 2>/dev/null || true
    ipcrm -S 0x12345679 2>/dev/null || true

    # 清理 POSIX 共享内存
    shm_names=(
        "/ipc_test_pool"
        "/ipc_test_events"
        "/ipc_test_soe"
        "/ipc_data_pool"
        "/ipc_events"
        "/test_stats_pool"
        "/test_stats_events"
        "/test_health_pool"
        "/test_health_events"
        "/test_snap_pool"
        "/test_snap_events"
        "/ipc_bench_pool"
        "/ipc_bench_events"
        "/ipc_bench_soe"
        "/ipc_voting_test"
        "/ipc_iec61850_test"
    )

    for name in "${shm_names[@]}"; do
        if [ -e "/dev/shm${name}" ]; then
            rm -f "/dev/shm${name}" 2>/dev/null || true
        fi
        shm_unlink "$name" 2>/dev/null || true
    done

    echo -e "${GREEN}共享内存清理完成${NC}"
}

# 编译项目
build_project() {
    echo -e "${BLUE}编译项目...${NC}"

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # 配置 CMake
    if [ ! -f "Makefile" ]; then
        cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
    fi

    # 编译
    make -j$(nproc)

    echo -e "${GREEN}编译完成${NC}"
}

# 运行单个测试
run_test() {
    local test_name=$1
    local test_binary="${BUILD_DIR}/tests/$test_name"

    if [ ! -f "$test_binary" ]; then
        echo -e "${YELLOW}警告: 测试 $test_name 不存在，跳过${NC}"
        return 1
    fi

    echo -e "${BLUE}运行测试: $test_name${NC}"

    if [ "$VERBOSE" = true ]; then
        "$test_binary" || {
            echo -e "${RED}测试 $test_name 失败${NC}"
            return 1
        }
    else
        "$test_binary" > "${TEST_RESULTS_DIR}/${test_name}.log" 2>&1 || {
            echo -e "${RED}测试 $test_name 失败${NC}"
            echo -e "${YELLOW}查看日志: ${TEST_RESULTS_DIR}/${test_name}.log${NC}"
            return 1
        }
    fi

    echo -e "${GREEN}测试 $test_name 通过${NC}"
    return 0
}

# 运行所有测试
run_all_tests() {
    mkdir -p "${TEST_RESULTS_DIR}"

    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    local skipped_tests=0

    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}开始执行单元测试${NC}"
    echo -e "${BLUE}========================================${NC}"

    if [ -n "$SPECIFIC_TEST" ]; then
        echo -e "${YELLOW}只运行测试: $SPECIFIC_TEST${NC}"
        if run_test "$SPECIFIC_TEST"; then
            ((passed_tests++))
        else
            ((failed_tests++))
        fi
        ((total_tests++))
    else
        # 运行基础测试
        echo ""
        echo -e "${BLUE}--- 基础测试 ---${NC}"
        for test in "${TESTS[@]}"; do
            if run_test "$test"; then
                ((passed_tests++))
            else
                ((failed_tests++))
            fi
            ((total_tests++))
        done

        # 运行 IEC61850 测试
        echo ""
        echo -e "${BLUE}--- IEC61850 测试 ---${NC}"
        for test in "${IEC61850_TESTS[@]}"; do
            if [ -f "${BUILD_DIR}/tests/$test" ]; then
                if run_test "$test"; then
                    ((passed_tests++))
                else
                    ((failed_tests++))
                fi
            else
                echo -e "${YELLOW}跳过测试 $test (未编译或需要 libiec61850)${NC}"
                ((skipped_tests++))
            fi
            ((total_tests++))
        done

        # 运行 Qt 测试
        echo ""
        echo -e "${BLUE}--- Qt 测试 ---${NC}"
        for test in "${QT_TESTS[@]}"; do
            if [ -f "${BUILD_DIR}/tests/$test" ]; then
                if run_test "$test"; then
                    ((passed_tests++))
                else
                    ((failed_tests++))
                fi
            else
                echo -e "${YELLOW}跳过测试 $test (未编译或需要 Qt5)${NC}"
                ((skipped_tests++))
            fi
            ((total_tests++))
        done
    fi

    # 打印结果摘要
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}测试结果摘要${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "总测试数: ${total_tests}"
    echo -e "${GREEN}通过: ${passed_tests}${NC}"
    if [ $failed_tests -gt 0 ]; then
        echo -e "${RED}失败: ${failed_tests}${NC}"
    fi
    if [ $skipped_tests -gt 0 ]; then
        echo -e "${YELLOW}跳过: ${skipped_tests}${NC}"
    fi

    # 生成测试报告
    generate_test_report "$total_tests" "$passed_tests" "$failed_tests" "$skipped_tests"

    # 返回失败数
    return $failed_tests
}

# 生成测试报告
generate_test_report() {
    local total=$1
    local passed=$2
    local failed=$3
    local skipped=$4

    local report_file="${TEST_RESULTS_DIR}/test_report.txt"

    cat > "$report_file" <<EOF
IPCSharedDataPool 单元测试报告
========================================
生成时间: $(date)

测试统计
--------
总测试数: ${total}
通过: ${passed}
失败: ${failed}
跳过: ${skipped}

测试日志
--------
EOF

    for test in "${TESTS[@]}" "${IEC61850_TESTS[@]}" "${QT_TESTS[@]}"; do
        if [ -f "${TEST_RESULTS_DIR}/${test}.log" ]; then
            echo "" >> "$report_file"
            echo "### ${test}" >> "$report_file"
            cat "${TEST_RESULTS_DIR}/${test}.log" >> "$report_file"
        fi
    done

    echo -e "${BLUE}测试报告已生成: ${report_file}${NC}"
}

# 列出所有测试
list_tests() {
    echo "可用的测试:"
    echo ""
    echo "基础测试:"
    for test in "${TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
    echo "IEC61850 测试 (需要 libiec61850):"
    for test in "${IEC61850_TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
    echo "Qt 测试 (需要 Qt5):"
    for test in "${QT_TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
    echo "其他:"
    echo "  - tst_benchmark (性能基准测试)"
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --clean)
                CLEAN_ONLY=true
                shift
                ;;
            --no-build)
                NO_BUILD=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --test)
                SPECIFIC_TEST="$2"
                shift 2
                ;;
            --list)
                list_tests
                exit 0
                ;;
            --help)
                print_help
                exit 0
                ;;
            *)
                echo -e "${RED}未知选项: $1${NC}"
                echo "使用 --help 查看帮助信息"
                exit 1
                ;;
        esac
    done
}

# 主函数
main() {
    # 解析参数
    parse_args "$@"

    # 只清理
    if [ "$CLEAN_ONLY" = true ]; then
        clean_shm
        exit 0
    fi

    # 清理旧的共享内存
    clean_shm

    # 编译项目
    if [ "$NO_BUILD" = false ]; then
        build_project
    fi

    # 运行测试
    local failed_count=0
    run_all_tests || failed_count=$?

    # 再次清理
    clean_shm

    # 根据结果退出
    if [ $failed_count -gt 0 ]; then
        echo -e "${RED}有 ${failed_count} 个测试失败${NC}"
        exit 1
    else
        echo -e "${GREEN}所有测试通过！${NC}"
        exit 0
    fi
}

# 执行主函数
main "$@"
