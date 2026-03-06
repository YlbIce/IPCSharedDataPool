#!/bin/bash
# IPCSharedDataPool 测试APP启动脚本
# 用法: ./run_apps.sh [comm|ui|business|all]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BIN_DIR="$BUILD_DIR"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 设置Qt环境
export QT_QPA_PLATFORM=xcb
export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"

# 打印帮助
print_help() {
    echo -e "${BLUE}IPCSharedDataPool 测试APP启动脚本${NC}"
    echo ""
    echo "用法: $0 [命令] [选项]"
    echo ""
    echo "命令:"
    echo "  build       构建所有APP"
    echo "  comm        启动通信进程 (创建数据池、模拟数据采集)"
    echo "  ui          启动UI进程 (数据显示、遥控操作、SOE查看)"
    echo "  business    启动业务进程 (事件处理、表决、告警监控)"
    echo "  all         启动所有进程（三个终端）"
    echo "  stop        停止所有进程"
    echo "  clean       清理共享内存"
    echo "  test        运行单元测试"
    echo "  help        显示帮助信息"
    echo ""
    echo "启动顺序建议:"
    echo "  1. 先启动通信进程 (comm_process) - 创建数据池"
    echo "  2. 再启动UI进程 (ui_process) - 连接数据池进行监控"
    echo "  3. 最后启动业务进程 (business_process) - 连接数据池进行业务处理"
    echo ""
    echo "示例:"
    echo "  $0 build          # 构建所有APP"
    echo "  $0 comm           # 启动通信进程"
    echo "  $0 all            # 在三个终端中启动所有进程"
    echo ""
}

# 构建APP
build_apps() {
    echo -e "${YELLOW}正在构建测试APP...${NC}"
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    cmake .. -DCMAKE_BUILD_TYPE=Release
    if [ $? -ne 0 ]; then
        echo -e "${RED}CMake配置失败!${NC}"
        exit 1
    fi
    
    make -j$(nproc)
    if [ $? -ne 0 ]; then
        echo -e "${RED}编译失败!${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}构建成功!${NC}"
    echo ""
}

# 检查可执行文件是否存在
check_executables() {
    if [ ! -f "$BIN_DIR/comm_process" ] || [ ! -f "$BIN_DIR/ui_process" ] || [ ! -f "$BIN_DIR/business_process" ]; then
        echo -e "${YELLOW}可执行文件不存在，正在构建...${NC}"
        build_apps
    fi
}

# 启动通信进程
start_comm() {
    check_executables
    check_shm_limits
    
    # 检查是否需要清理旧共享内存
    if [ -f /dev/shm/ipc_data_pool ]; then
        echo -e "${YELLOW}发现旧的共享内存，正在清理...${NC}"
        clean_shm
    fi
    
    echo -e "${GREEN}启动通信进程...${NC}"
    cd "$BIN_DIR"
    export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"
    ./comm_process "$@"
}

# 启动UI进程
start_ui() {
    check_executables
    check_pool_exists
    echo -e "${GREEN}启动UI进程...${NC}"
    cd "$BIN_DIR"
    export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"
    ./ui_process "$@"
}

# 启动业务进程
start_business() {
    check_executables
    echo -e "${GREEN}启动业务进程...${NC}"
    cd "$BIN_DIR"
    export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"
    ./business_process "$@"
}

# 启动所有进程
start_all() {
    check_executables
    
    echo -e "${GREEN}在三个终端中启动所有进程...${NC}"
    echo -e "${YELLOW}启动顺序: 通信进程 -> UI进程 -> 业务进程${NC}"
    echo ""
    
    export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"
    
    # 尝试不同的终端模拟器
    if command -v gnome-terminal &> /dev/null; then
        gnome-terminal --title="通信进程" -- bash -c "cd '$BIN_DIR' && ./comm_process; exec bash"
        sleep 2
        gnome-terminal --title="UI进程" -- bash -c "cd '$BIN_DIR' && ./ui_process; exec bash"
        sleep 1
        gnome-terminal --title="业务进程" -- bash -c "cd '$BIN_DIR' && ./business_process; exec bash"
    elif command -v konsole &> /dev/null; then
        konsole --new-tab -e bash -c "cd '$BIN_DIR' && ./comm_process; exec bash" &
        sleep 2
        konsole --new-tab -e bash -c "cd '$BIN_DIR' && ./ui_process; exec bash" &
        sleep 1
        konsole --new-tab -e bash -c "cd '$BIN_DIR' && ./business_process; exec bash" &
    elif command -v xterm &> /dev/null; then
        xterm -T "通信进程" -e "cd '$BIN_DIR' && ./comm_process; bash" &
        sleep 2
        xterm -T "UI进程" -e "cd '$BIN_DIR' && ./ui_process; bash" &
        sleep 1
        xterm -T "业务进程" -e "cd '$BIN_DIR' && ./business_process; bash" &
    else
        echo -e "${RED}未找到支持的终端模拟器，请手动启动各进程${NC}"
        echo "  终端1: $BIN_DIR/comm_process"
        echo "  终端2: $BIN_DIR/ui_process"
        echo "  终端3: $BIN_DIR/business_process"
        exit 1
    fi
    
    echo -e "${GREEN}所有进程已启动!${NC}"
}

# 停止所有进程
stop_all() {
    echo -e "${YELLOW}停止所有进程...${NC}"
    pkill -f comm_process 2>/dev/null
    pkill -f ui_process 2>/dev/null
    pkill -f business_process 2>/dev/null
    echo -e "${GREEN}所有进程已停止${NC}"
}

# 清理共享内存
clean_shm() {
    echo -e "${YELLOW}清理共享内存...${NC}"
    
    # 删除共享内存对象
    rm -f /dev/shm/ipc_data_pool
    rm -f /dev/shm/ipc_events
    rm -f /dev/shm/ipc_soe
    rm -f /dev/shm/ipc_voting
    rm -f /dev/shm/sem.ipc_data_pool_lock
    rm -f /dev/shm/sem.ipc_events_lock
    rm -f /dev/shm/sem.ipc_soe_lock
    
    echo -e "${GREEN}共享内存已清理${NC}"
}

# 检查系统共享内存限制
check_shm_limits() {
    echo -e "${BLUE}检查系统共享内存限制...${NC}"
    
    # 检查/dev/shm大小
    local shm_size=$(df -BG /dev/shm 2>/dev/null | awk 'NR==2 {print $2}')
    echo "  /dev/shm 大小: $shm_size"
    
    # 检查最大共享内存段大小
    local max_shm=$(cat /proc/sys/kernel/shmmax 2>/dev/null)
    if [ -n "$max_shm" ]; then
        local max_shm_mb=$((max_shm / 1024 / 1024))
        echo "  最大共享内存段: ${max_shm_mb}MB"
    fi
    
    # 检查当前使用的共享内存
    local used_shm=$(df -BG /dev/shm 2>/dev/null | awk 'NR==2 {print $3}')
    echo "  已使用: $used_shm"
    
    # 检查是否有残留的共享内存
    if [ -f /dev/shm/ipc_data_pool ]; then
        local pool_size=$(ls -lh /dev/shm/ipc_data_pool 2>/dev/null | awk '{print $5}')
        echo -e "  ${YELLOW}发现残留数据池: $pool_size${NC}"
        echo -e "  ${YELLOW}建议运行: $0 clean${NC}"
    fi
    
    echo ""
}

# 检查数据池是否存在
check_pool_exists() {
    if [ ! -f /dev/shm/ipc_data_pool ]; then
        echo -e "${RED}错误: 数据池不存在!${NC}"
        echo -e "${YELLOW}请先启动通信进程 (comm_process) 创建数据池${NC}"
        exit 1
    fi
    
    local pool_size=$(ls -lh /dev/shm/ipc_data_pool 2>/dev/null | awk '{print $5}')
    echo -e "${GREEN}数据池已存在, 大小: $pool_size${NC}"
}

# 运行测试
run_tests() {
    echo -e "${YELLOW}运行单元测试...${NC}"
    cd "$BIN_DIR"
    export LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH"
    
    echo ""
    echo "=== tst_common ==="
    ./tst_common
    
    echo ""
    echo "=== tst_shm_pool ==="
    ./tst_shm_pool
    
    echo ""
    echo "=== tst_event_center ==="
    ./tst_event_center
    
    echo ""
    echo -e "${GREEN}所有测试完成!${NC}"
}

# 主逻辑
case "$1" in
    build)
        build_apps
        ;;
    comm)
        shift
        start_comm "$@"
        ;;
    ui)
        shift
        start_ui "$@"
        ;;
    business)
        shift
        start_business "$@"
        ;;
    all)
        start_all
        ;;
    stop)
        stop_all
        ;;
    clean)
        clean_shm
        ;;
    test)
        run_tests
        ;;
    help|--help|-h)
        print_help
        ;;
    *)
        if [ -z "$1" ]; then
            print_help
        else
            echo -e "${RED}未知命令: $1${NC}"
            print_help
            exit 1
        fi
        ;;
esac
