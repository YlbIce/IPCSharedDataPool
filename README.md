# IPCSharedDataPool - 电力系统进程间共享数据池与事件中心

跨进程共享数据池和事件中心组件，专为Linux工控机多进程应用场景设计，适用于电力系统SCADA/DCS应用。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)]()

## 项目简介

IPCSharedDataPool是一个高性能的跨进程共享内存数据池和事件中心组件，专为电力系统实时应用设计。它提供了高效的数据共享、事件通知、SOE记录、数据持久化等核心功能，支持多进程协作的电力系统应用架构。

## 核心特性

### 基础功能
- **共享内存数据池**：支持百万级数据点，SoA（Structure of Arrays）布局提高缓存效率
- **事件中心**：多生产者多消费者事件队列，支持跨进程发布订阅模式
- **跨进程同步**：基于pthread_rwlock的跨进程读写锁，支持读写分离
- **类型支持**：YX(遥信)、YC(遥测)、DZ(定值)、YK(遥控)四种电力系统标准数据类型
- **零依赖**：核心库仅依赖POSIX API，无需第三方库

### 高级功能
- **SOE记录器**：顺序事件记录（Sequence of Events），支持事件时间戳和持久化
- **持久化存储**：支持数据定期保存到SQLite数据库，异常恢复时加载历史数据
- **投票引擎**：多源数据投票机制，支持2取2、3取2等表决逻辑
- **IEC61850映射**：提供IEC61850标准数据模型映射，支持IEC61850通信
- **环形缓冲区**：高效的无锁环形缓冲区，用于事件队列管理
- **质量码支持**：完善的数据质量码体系，支持数据有效性判断

### 性能特性
- **高性能**：共享内存零拷贝，百万级数据点读写延迟<10μs
- **高并发**：支持多进程并发访问，读写锁保证数据一致性
- **低延迟**：事件通知延迟<100μs，满足实时系统要求
- **可扩展**：支持动态扩展数据点数量，无需重启系统

## 快速开始

### 环境要求

- **操作系统**：Linux (内核3.10+)
- **编译器**：GCC 7.0+ 或 Clang 5.0+ (支持C++17)
- **构建工具**：CMake 3.15+ 或 Qt 5.12+
- **依赖库**：
  - pthread (POSIX线程库)
  - rt (实时扩展库)
  - Qt5 (可选，用于UI示例程序)
  - SQLite3 (可选，用于持久化功能)

### 编译安装

#### 使用CMake编译

```bash
cd IPCSharedDataPool

# 创建构建目录
mkdir -p build && cd build

# 配置CMake
cmake ..

# 编译
make -j$(nproc)

# 运行测试
ctest

# 安装（可选）
sudo make install
```

#### 使用Qt Creator编译

```bash
# 打开Qt Creator，加载示例项目
cd examples/ui_process
qmake ui_process.pro
make
./ui_process
```

### 编译单元测试

```bash
cd IPCSharedDataPool

# 编译并运行基础测试
g++ -std=c++17 -pthread -o tests/tst_ringbuffer tests/tst_ringbuffer.cpp
./tests/tst_ringbuffer

# 编译并运行共享数据池测试
g++ -std=c++17 -pthread -lrt -o tests/tst_shm_pool tests/tst_shm_pool.cpp src/SharedDataPool.cpp
./tests/tst_shm_pool

# 编译并运行事件中心测试
g++ -std=c++17 -pthread -lrt -o tests/tst_event_center tests/tst_event_center.cpp src/IPCEventCenter.cpp
./tests/tst_event_center

# 编译并运行集成测试
g++ -std=c++17 -pthread -lrt -o tests/tst_integration tests/tst_integration.cpp src/DataPoolClient.cpp src/SharedDataPool.cpp src/IPCEventCenter.cpp
./tests/tst_integration

# 编译并运行SOE持久化测试
g++ -std=c++17 -pthread -lrt -o tests/tst_soe_persist tests/tst_soe_persist.cpp src/SOERecorder.cpp src/PersistentStorage.cpp
./tests/tst_soe_persist

# 编译并运行投票和IEC61850测试
g++ -std=c++17 -pthread -lrt -o tests/tst_voting_iec61850 tests/tst_voting_iec61850.cpp src/VotingEngine.cpp src/IEC61850Mapper.cpp
./tests/tst_voting_iec61850
```

### 运行示例程序

#### CMake构建的示例程序

```bash
cd IPCSharedDataPool/build

# 运行所有示例进程
./run_apps.sh
```

#### Qt示例程序

```bash
cd IPCSharedDataPool/examples

# 编译所有进程
./run_test.sh --build

# 运行多进程测试（UI进程为图形界面）
./run_test.sh
```

**示例进程说明：**
- **通信进程（comm_process）**：模拟前端通信，接收外部数据并写入数据池
- **业务进程（business_process）**：模拟业务逻辑，从数据池读取数据并进行处理
- **UI进程（ui_process）**：图形界面，显示数据池状态和实时数据更新

**UI进程图形界面功能：**
- 状态栏显示连接状态和更新时间
- 数据池状态表格（点计数统计、YX/YC数据）
- 运行日志文本浏览器
- 刷新数据、清除日志按钮
- 实时数据曲线显示

## 使用示例

### 1. 创建数据池（主进程）

```cpp
#include "DataPoolClient.h"

using namespace IPC;

int main() {
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "main_process";
    config.yxCount = 10000;  // 遥信点数
    config.ycCount = 10000;  // 遥测点数
    config.create = true;    // 创建模式
    
    DataPoolClient* client = DataPoolClient::init(config);
    if (!client) return -1;
    
    // 注册数据点
    uint32_t idx;
    client->registerPoint(makeKey(1, 0), PointType::YX, idx);
    
    // 写入数据
    client->setYX(makeKey(1, 0), 1);
    
    // 发布事件
    client->publishEvent(makeKey(1, 0), PointType::YX, 
                         uint32_t(0), uint32_t(1));
    
    // ...
    
    client->shutdown();
    delete client;
    return 0;
}
```

### 2. 连接数据池（从进程）

```cpp
#include "DataPoolClient.h"

using namespace IPC;

int main() {
    DataPoolClient::Config config;
    config.eventName = "/ipc_events";
    config.processName = "worker_process";
    config.create = false;   // 连接模式
    
    DataPoolClient* client = DataPoolClient::init(config);
    if (!client) return -1;
    
    // 订阅事件
    uint32_t subId = client->subscribe([](const Event& e) {
        printf("Event: key=%lu, type=%d\n", e.key, (int)e.pointType);
    });
    
    // 处理事件
    while (true) {
        client->processEvents(subId, 100);
        usleep(10000);
    }
    
    client->shutdown();
    delete client;
    return 0;
}
```

## 项目结构

```
IPCSharedDataPool/
├── include/              # 头文件
│   ├── Common.h          # 通用定义和类型
│   ├── SharedDataPool.h  # 共享数据池
│   ├── IPCEventCenter.h  # 事件中心
│   ├── ShmRingBuffer.h   # 环形缓冲区
│   ├── ProcessRWLock.h   # 跨进程读写锁
│   ├── DataPoolClient.h  # 客户端封装
│   ├── SOERecorder.h     # SOE记录器
│   ├── PersistentStorage.h # 持久化存储
│   ├── VotingEngine.h    # 投票引擎
│   └── IEC61850Mapper.h  # IEC61850映射
├── src/                  # 源文件
│   ├── SharedDataPool.cpp
│   ├── IPCEventCenter.cpp
│   ├── DataPoolClient.cpp
│   ├── SOERecorder.cpp
│   ├── PersistentStorage.cpp
│   ├── VotingEngine.cpp
│   └── IEC61850Mapper.cpp
├── tests/                # 单元测试
│   ├── tst_common.cpp         # 通用测试
│   ├── tst_rwlock.cpp         # 读写锁测试
│   ├── tst_ringbuffer.cpp     # 环形缓冲区测试
│   ├── tst_shm_pool.cpp       # 共享数据池测试
│   ├── tst_event_center.cpp   # 事件中心测试
│   ├── tst_integration.cpp    # 集成测试
│   ├── tst_benchmark.cpp      # 性能基准测试
│   ├── tst_soe_persist.cpp    # SOE持久化测试
│   └── tst_voting_iec61850.cpp # 投票和IEC61850测试
├── apps/                  # 应用程序（CMake构建）
│   ├── comm_process/     # 通信进程
│   ├── business_process/ # 业务进程
│   └── ui_process/       # UI进程
├── examples/             # 示例程序（Qt构建）
│   ├── comm_process/     # 通信进程（命令行）
│   ├── business_process/ # 业务进程（命令行）
│   └── ui_process/       # UI进程（图形界面）
├── pugixml/              # XML解析库（第三方）
├── docs/                 # 文档
│   └── PROJECT_PLAN.md   # 项目计划
├── CMakeLists.txt        # CMake构建配置
├── README.md             # 项目说明
├── USER_MANUAL.md        # 用户手册
└── run_apps.sh           # 运行脚本
```

## API参考

### DataPoolClient（推荐使用）

`DataPoolClient`是推荐使用的高级API，提供了完整的客户端封装。

| 方法 | 说明 |
|------|------|
| `init(config)` | 初始化客户端，创建或连接共享数据池 |
| `shutdown()` | 关闭客户端，释放资源 |
| `setYX(key, value)` | 设置遥信值 |
| `getYX(key, value, quality)` | 获取遥信值和质量码 |
| `setYC(key, value)` | 设置遥测值 |
| `getYC(key, value, quality)` | 获取遥测值和质量码 |
| `setDZ(key, value)` | 设置定值 |
| `getDZ(key, value, quality)` | 获取定值和质量码 |
| `registerPoint(key, type, index)` | 注册数据点，获取索引 |
| `subscribe(callback)` | 订阅事件，返回订阅ID |
| `unsubscribe(subId)` | 取消订阅 |
| `publishEvent(key, type, oldValue, newValue)` | 发布数据变化事件 |
| `processEvents(subId, max)` | 处理订阅的事件 |
| `getStats()` | 获取数据池统计信息 |

### SOERecorder（SOE记录器）

```cpp
// 创建SOE记录器
SOERecorder recorder;

// 记录SOE事件
recorder.recordEvent(key, eventType, oldValue, newValue, timestamp);

// 获取SOE记录
std::vector<SOEEvent> events = recorder.getEvents(startTime, endTime);

// 导出SOE记录
recorder.exportToFile("soe_records.csv");
```

### PersistentStorage（持久化存储）

```cpp
// 创建持久化存储
PersistentStorage storage;

// 配置存储参数
PersistentStorage::Config config;
config.dbPath = "data_pool.db";
config.saveInterval = 60000; // 60秒
storage.init(config);

// 保存数据快照
storage.saveSnapshot();

// 加载历史数据
storage.loadSnapshot();
```

### VotingEngine（投票引擎）

```cpp
// 创建投票引擎
VotingEngine engine;

// 配置投票策略
VotingEngine::Config config;
config.type = VotingType::TwoOutOfTwo;
config.sources = {1, 2};
engine.init(config);

// 执行投票
uint32_t result = engine.vote(values);
```

### SharedDataPool（底层API）

直接操作共享内存数据池，支持按索引访问（高性能场景）。

```cpp
// 创建共享数据池
SharedDataPool pool;
pool.create("/ipc_pool", yxCount, ycCount);

// 按索引访问（高性能）
pool.setYXByIndex(index, value);
pool.getYXByIndex(index, value, quality, timestamp);
```

### IPCEventCenter（底层API）

直接操作事件中心，支持多订阅者、事件过滤等高级功能。

```cpp
// 创建事件中心
IPCEventCenter events;
events.create("/ipc_events");

// 发布事件
Event e;
e.key = key;
e.pointType = PointType::YX;
events.publish(e);

// 订阅事件
uint32_t subId = events.subscribe([](const Event& e) {
    // 处理事件
});
```

## 内存布局

```
共享内存布局:
┌──────────────────────────────────────┐
│              ShmHeader               │  头部信息（版本、大小、统计）
├──────────────────────────────────────┤
│              YX Data                 │  遥信数据 (SoA布局)
│   values[] | timestamps[] | qualities[]
├──────────────────────────────────────┤
│              YC Data                 │  遥测数据 (SoA布局)
│   values[] | timestamps[] | qualities[]
├──────────────────────────────────────┤
│              DZ Data                 │  定值数据 (SoA布局)
│   values[] | timestamps[] | qualities[]
├──────────────────────────────────────┤
│              Index Table             │  哈希索引表（Key -> Index）
├──────────────────────────────────────┤
│              Process Info            │  进程信息（心跳、连接数）
└──────────────────────────────────────┘
```

## 性能指标

| 指标 | 数值 | 说明 |
|------|------|------|
| 数据点容量 | 1,000,000+ | 支持百万级数据点 |
| 读写延迟 | <10μs | 共享内存零拷贝 |
| 事件通知延迟 | <100μs | 跨进程事件通知 |
| 并发访问 | 16+ 进程 | 支持多进程并发 |
| 内存占用 | ~50MB/百万点 | YX+YC各50万点 |
| 吞吐量 | >10M ops/s | 单进程操作吞吐量 |

## 应用场景

### 电力系统应用
- **SCADA系统**：前端通信与业务处理分离，提高系统可靠性
- **DCS系统**：分布式控制，多进程协作
- **保护装置**：实时数据共享，多源数据融合
- **调度系统**：跨进程数据共享，降低耦合度

### 工业控制应用
- **PLC系统**：多进程实时数据交换
- **MES系统**：生产数据实时共享
- **工业网关**：协议转换，数据汇聚

## 设计原则

1. **高性能**：共享内存零拷贝，SoA布局提高缓存效率
2. **高可靠**：进程隔离，单个进程崩溃不影响其他进程
3. **低延迟**：事件驱动，实时数据推送
4. **易扩展**：模块化设计，支持动态扩展
5. **易使用**：提供高级API，简化应用开发

## 技术亮点

1. **SoA布局**：将同类型数据连续存储，提高缓存命中率
2. **哈希索引**：Key-Hash映射，O(1)查找性能
3. **读写锁**：支持多读单写，提高并发性能
4. **环形缓冲区**：无锁设计，高并发事件队列
5. **质量码**：完善的数据质量体系，支持数据有效性判断
6. **跨进程同步**：基于共享内存的进程间同步机制

## 常见问题

### Q: 如何确定数据点数量？
A: 根据实际应用需求，建议预留20-30%的扩展空间。

### Q: 多进程如何保证数据一致性？
A: 使用跨进程读写锁，写操作独占，读操作共享。

### Q: 事件通知是否会丢失？
A: 使用环形缓冲区，满时覆盖最旧事件，建议及时处理事件。

### Q: 如何实现数据持久化？
A: 使用PersistentStorage组件，定期保存数据快照。

### Q: 支持Windows平台吗？
A: 目前仅支持Linux平台，依赖POSIX API。

## 相关文档

- [用户手册](USER_MANUAL.md) - 详细的使用说明和API文档
- [项目计划](docs/PROJECT_PLAN.md) - 项目开发计划和技术路线
- [设计文档](docs/IPCSharedDataPool_Design.md) - 系统架构和设计细节

## 贡献指南

欢迎提交Issue和Pull Request！

1. Fork本项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交Pull Request

## 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

## 联系方式

- 项目主页：https://github.com/YlbIce/IPCSharedDataPool
- 问题反馈：https://github.com/YlbIce/IPCSharedDataPool/issues

## 致谢

感谢所有贡献者的支持！

## 更新日志

### v1.0.0 (2026-03-05)
- 初始版本发布
- 实现核心数据池和事件中心
- 支持SOE记录和持久化
- 实现投票引擎和IEC61850映射
- 提供完整的示例程序和测试用例
