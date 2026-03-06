#!/bin/bash
# 多进程测试脚本
# 使用方法: ./run_test.sh [选项]
#   --build   先编译再运行
#   --clean   清理共享内存后运行

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# 清理函数
cleanup() {
    echo "Cleaning up..."
    rm -f /dev/shm/ipc_data_pool 2>/dev/null
    rm -f /dev/shm/ipc_events 2>/dev/null
    
    # 终止后台进程
    kill $(jobs -p) 2>/dev/null
    wait 2>/dev/null
}

# 注册清理函数
trap cleanup EXIT

# 解析参数
BUILD=false
CLEAN=false

for arg in "$@"; do
    case $arg in
        --build) BUILD=true ;;
        --clean) CLEAN=true ;;
    esac
done

# 清理
if [ "$CLEAN" = true ]; then
    cleanup
fi

# 编译
if [ "$BUILD" = true ]; then
    echo "Building..."
    mkdir -p "$BUILD_DIR"
    
    cd "$SCRIPT_DIR/comm_process"
    qmake && make clean && make
    if [ $? -ne 0 ]; then echo "Build comm_process failed"; exit 1; fi
    mv comm_process "$BUILD_DIR/"
    
    cd "$SCRIPT_DIR/business_process"
    qmake && make clean && make
    if [ $? -ne 0 ]; then echo "Build business_process failed"; exit 1; fi
    mv business_process "$BUILD_DIR/"
    
    cd "$SCRIPT_DIR/ui_process"
    qmake && make clean && make
    if [ $? -ne 0 ]; then echo "Build ui_process failed"; exit 1; fi
    mv ui_process "$BUILD_DIR/"
    
    echo "Build complete."
fi

# 检查可执行文件
if [ ! -f "$BUILD_DIR/comm_process" ] || [ ! -f "$BUILD_DIR/business_process" ] || [ ! -f "$BUILD_DIR/ui_process" ]; then
    echo "Error: Executables not found. Run with --build first."
    exit 1
fi

echo ""
echo "========================================"
echo "  IPCSharedDataPool Multi-Process Test"
echo "========================================"
echo ""

# 启动通信进程（数据生产者）
echo "Starting comm_process..."
"$BUILD_DIR/comm_process" --yx 500 --yc 500 --interval 50 &
COMM_PID=$!

# 等待数据池创建
sleep 1

# 启动业务进程（事件订阅者）
echo "Starting business_process..."
"$BUILD_DIR/business_process" &
BUSINESS_PID=$!

# 等待订阅完成
sleep 0.5

# 启动UI进程（数据显示）
echo "Starting ui_process (press Ctrl+C to exit)..."
echo ""
sleep 1

"$BUILD_DIR/ui_process" --interval 2000

# 等待
wait
