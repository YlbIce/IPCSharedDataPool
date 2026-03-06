# IPCSharedDataPool 使用手册

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 核心特性](#2-核心特性)
- [3. 系统架构](#3-系统架构)
- [4. 快速开始](#4-快速开始)
- [5. 详细 API 文档](#5-详细-api-文档)
- [6. 高级功能](#6-高级功能)
- [7. 最佳实践](#7-最佳实践)
- [8. 故障排除](#8-故障排除)

---

## 1. 项目概述

### 1.1 项目简介

**IPCSharedDataPool** 是一个专为 Linux 工控机设计的跨进程共享数据池和事件中心组件，适用于电力自动化、工业控制等对实时性和可靠性要求极高的场景。

### 1.2 解决的问题

- **数据孤岛问题**: 多进程间需要高效共享实时数据
- **性能瓶颈**: 传统 IPC 方式（管道、消息队列）性能不足
- **同步复杂性**: 跨进程数据一致性难以保证
- **事件通知缺失**: 数据变化无法及时通知订阅者

### 1.3 适用场景

- ✅ 电力系统 SCADA 应用
- ✅ 工业自动化数据采集
- ✅ 多进程协同处理系统
- ✅ 实时数据发布/订阅平台
- ✅ 分布式控制系统

### 1.4 技术规格

| 项目 | 规格 |
|------|------|
| 编程语言 | C++17 |
| 系统依赖 | POSIX (Linux) |
| 第三方库 | pugixml (可选，用于 XML 解析) |
| 最大数据点数 | 每类型 100 万点 |
| 单点访问延迟 | < 100ns |
| 事件吞吐能力 | > 1M events/s |
| 时间戳精度 | 纳秒级 |

---

## 2. 核心特性

### 2.1 共享内存数据池

- **SoA (Structure of Arrays) 布局**: 提升 CPU 缓存命中率
- **四类数据类型支持**:
  - **YX (遥信)**: 开关状态 (uint8_t)
  - **YC (遥测)**: 模拟量测量值 (float)
  - **DZ (定值)**: 保护定值参数 (float)
  - **YK (遥控)**: 遥控输出 (uint8_t)
- **跨进程读写锁**: 基于 pthread_rwlock 的安全访问
- **索引映射机制**: O(1) 复杂度的快速查找

### 2.2 事件中心

- **发布/订阅模式**: 解耦生产者和消费者
- **环形缓冲区**: 无锁设计，高性能
- **多订阅者支持**: 每个订阅者独立消费队列
- **事件过滤**: 支持按类型、点位过滤

### 2.3 SOE 事件记录

- **高分辨率时标**: 纳秒级精度，分辨率 < 1ms
- **环形缓冲存储**: 自动覆盖最旧记录
- **条件查询**: 支持时间范围、点位类型等多维度查询
- **标准格式导出**: 支持 CSV、COMTRADE 格式

### 2.4 持久化存储

- **快照机制**: 定期保存数据到磁盘
- **掉电保持**: 系统重启后恢复关键数据
- **备份管理**: 支持多个历史版本

### 2.5 三取二表决

- **冗余容错**: 三通道数据表决算法
- **偏差检测**: 自动识别异常通道
- **告警输出**: 偏差超限时触发告警

### 2.6 IEC 61850 映射

- **逻辑节点映射**: LN/DO/DA层次化映射
- **SCL 文件解析**: 支持变电站配置语言
- **数据集定义**: 灵活的数据组合

---

## 3. 系统架构

### 3.1 整体架构图

```
┌─────────────────────────────────────────────────┐
│              应用层                              │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐   │
│  │通信进程   │  │业务进程   │  │UI进程    │   │
│  │(生产者)   │  │(消费者)   │  │(显示)     │   │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘   │
└────────┼──────────────┼──────────────┼─────────┘
         │              │              │
┌────────▼──────────────▼──────────────▼─────────┐
│          DataPoolClient (统一接口)              │
├─────────────────────────────────────────────────┤
│  ┌──────────────┐      ┌──────────────┐        │
│  │SharedDataPool│◄────►│IPCEventCenter│        │
│  │(数据池)      │      │(事件中心)    │        │
│  └──────────────┘      └──────────────┘        │
│  ┌──────────────┐      ┌──────────────┐        │
│  │SOERecorder   │      │PersistentStg │        │
│  │(SOE 记录)    │      │(持久化)      │        │
│  └──────────────┘      └──────────────┘        │
├─────────────────────────────────────────────────┤
│           POSIX 共享内存 + 进程间同步            │
└─────────────────────────────────────────────────┘
```

### 3.2 共享内存布局

```
共享内存结构 (/dev/shm/ipc_data_pool):
┌──────────────────────────────────────┐
│         ShmHeader (头部)             │  64 bytes
│  - 魔数、版本、计数                  │
│  - 各区域偏移量                      │
│  - 跨进程读写锁                      │
├──────────────────────────────────────┤
│         YX Data Area                 │  SoA 布局
│  values[10000] | timestamps[10000]   │
│  | qualities[10000]                  │
├──────────────────────────────────────┤
│         YC Data Area                 │  SoA 布局
│  values[10000] | timestamps[10000]   │
│  | qualities[10000]                  │
├──────────────────────────────────────┤
│         DZ Data Area                 │  同 YC 结构
├──────────────────────────────────────┤
│         YK Data Area                 │  同 YX 结构
├──────────────────────────────────────┤
│         Index Table                  │
│  - Hash Table (哈希表)               │
│  - Index Entries (索引条目)          │
├──────────────────────────────────────┤
│         Process Info Array           │
│  - 进程注册信息                      │
│  - 心跳状态                          │
└──────────────────────────────────────┘
```

### 3.3 数据流图

```
数据采集流程:
传感器 → 通信进程 → setYX/setYC → 共享内存
                                    ↓
                              发布事件
                                    ↓
                        ┌───────────┴───────────┐
                        ↓                       ↓
                  业务进程订阅            UI进程订阅
                   处理逻辑                刷新显示
```

---

## 4. 快速开始

### 4.1 环境要求

- **操作系统**: Linux (推荐 Ubuntu 18.04+)
- **编译器**: g++ 7+ (支持 C++17)
- **必需库**: pthread, rt (POSIX realtime)
- **可选库**: Qt5 (测试程序), pugixml (XML 解析)

### 4.2 编译项目

#### 方式一：手动编译单个模块

```bash
cd IPCSharedDataPool

# 编译基础测试
g++ -std=c++17 -pthread -o tests/tst_ringbuffer \
    tests/tst_ringbuffer.cpp
./tests/tst_ringbuffer

# 编译数据池测试
g++ -std=c++17 -pthread -lrt -o tests/tst_shm_pool \
    tests/tst_shm_pool.cpp src/SharedDataPool.cpp
./tests/tst_shm_pool

# 编译事件中心测试
g++ -std=c++17 -pthread -lrt -o tests/tst_event_center \
    tests/tst_event_center.cpp src/IPCEventCenter.cpp
./tests/tst_event_center

# 编译集成测试
g++ -std=c++17 -pthread -lrt -o tests/tst_integration \
    tests/tst_integration.cpp \
    src/DataPoolClient.cpp src/SharedDataPool.cpp \
    src/IPCEventCenter.cpp
./tests/tst_integration
```

#### 方式二：使用 CMake (如果提供 CMakeLists.txt)

```bash
mkdir build && cd build
cmake ..
make
make test
```

#### 方式三：编译 Qt 示例程序

```bash
cd examples

# 编译所有示例
./run_test.sh --build

# 运行多进程测试
./run_test.sh
```

### 4.3 第一个程序：创建数据池

```cpp
#include "DataPoolClient.h"
#include <iostream>

using namespace IPC;

int main() {
    // 配置参数
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";      // 共享内存名称
    config.eventName = "/ipc_events";        // 事件中心名称
    config.processName = "my_first_app";     // 进程名称
    config.yxCount = 1000;                   // YX 点数
    config.ycCount = 1000;                   // YC 点数
    config.dzCount = 100;                    // DZ 点数
    config.ykCount = 100;                    // YK 点数
    config.create = true;                    // 创建模式
    
    // 初始化客户端
    DataPoolClient* client = DataPoolClient::init(config);
    if (!client) {
        std::cerr << "初始化失败!" << std::endl;
        return -1;
    }
    
    std::cout << "数据池创建成功!" << std::endl;
    std::cout << "YX 容量：" << client->getDataPool()->getYXCount() << std::endl;
    std::cout << "YC 容量：" << client->getDataPool()->getYCCount() << std::endl;
    
    // 注册数据点
    uint32_t yxIndex, ycIndex;
    client->registerPoint(makeKey(1, 0), PointType::YX, yxIndex);
    client->registerPoint(makeKey(2, 0), PointType::YC, ycIndex);
    
    // 写入数据
    client->setYX(makeKey(1, 0), 1);  // 设置遥信为合位
    client->setYC(makeKey(2, 0), 220.5f);  // 设置电压 220.5V
    
    // 读取数据
    uint8_t yxValue;
    uint8_t quality;
    if (client->getYX(makeKey(1, 0), yxValue, quality)) {
        std::cout << "YX 值：" << (int)yxValue 
                  << ", 质量码：" << (int)quality << std::endl;
    }
    
    // 清理资源
    client->shutdown();
    delete client;
    
    return 0;
}
```

**编译运行**:
```bash
g++ -std=c++17 -pthread -lrt -o myapp myapp.cpp \
    src/DataPoolClient.cpp src/SharedDataPool.cpp src/IPCEventCenter.cpp
./myapp
```

### 4.4 多进程通信示例

#### 进程 A：数据生产者

```cpp
// producer.cpp
#include "DataPoolClient.h"
#include <thread>
#include <chrono>

using namespace IPC;

int main() {
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "producer";
    config.yxCount = 100;
    config.ycCount = 100;
    config.create = true;  // 创建共享内存
    
    DataPoolClient* client = DataPoolClient::init(config);
    
    // 注册点位
    for (int i = 0; i < 100; i++) {
        uint32_t idx;
        client->registerPoint(makeKey(1, i), PointType::YX, idx);
        client->registerPoint(makeKey(2, i), PointType::YC, idx);
    }
    
    // 循环更新数据
    int counter = 0;
    while (true) {
        // 更新遥信
        for (int i = 0; i < 10; i++) {
            uint32_t idx = counter % 100;
            client->setYXByIndex(idx, counter % 2);
        }
        
        // 更新遥测
        for (int i = 0; i < 10; i++) {
            uint32_t idx = counter % 100;
            client->setYCByIndex(idx, 220.0f + counter * 0.1f);
        }
        
        counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    client->shutdown();
    delete client;
    return 0;
}
```

#### 进程 B：数据消费者

```cpp
// consumer.cpp
#include "DataPoolClient.h"
#include <iostream>

using namespace IPC;

int main() {
    DataPoolClient::Config config;
    config.poolName = "/ipc_data_pool";
    config.eventName = "/ipc_events";
    config.processName = "consumer";
    config.create = false;  // 连接已有共享内存
    
    DataPoolClient* client = DataPoolClient::init(config);
    
    // 订阅事件
    uint32_t subId = client->subscribe([](const Event& e) {
        std::cout << "收到事件：" 
                  << "点位=" << e.key 
                  << ", 类型=" << (int)e.pointType
                  << ", 新值=" << e.newValue << std::endl;
    });
    
    std::cout << "开始监听数据变化..." << std::endl;
    
    // 处理事件循环
    while (true) {
        client->processEvents(subId, 100);  // 每次最多处理 100 个事件
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 定期读取最新数据
        static int printCounter = 0;
        if (++printCounter % 100 == 0) {
            float ycValue;
            uint8_t quality;
            if (client->getYC(makeKey(2, 0), ycValue, quality)) {
                std::cout << "当前电压：" << ycValue << "V" << std::endl;
            }
        }
    }
    
    client->unsubscribe(subId);
    client->shutdown();
    delete client;
    return 0;
}
```

**编译运行**:
```bash
# 编译两个进程
g++ -std=c++17 -pthread -lrt -o producer producer.cpp \
    src/DataPoolClient.cpp src/SharedDataPool.cpp src/IPCEventCenter.cpp

g++ -std=c++17 -pthread -lrt -o consumer consumer.cpp \
    src/DataPoolClient.cpp src/SharedDataPool.cpp src/IPCEventCenter.cpp

# 先启动生产者（后台运行）
./producer &

# 再启动消费者
./consumer

# 停止后台进程
killall producer
```

---

## 5. 详细 API 文档

### 5.1 基础类型和枚举

#### PointType 枚举

```cpp
enum class PointType : uint8_t {
    YX = 0,     // 遥信 - 开关状态 (uint8_t)
    YC = 1,     // 遥测 - 模拟量 (float)
    DZ = 2,     // 定值 - 参数设定 (float)
    YK = 3      // 遥控 - 控制输出 (uint8_t)
};
```

#### Result 枚举

```cpp
enum class Result : int {
    OK = 0,              // 成功
    ERROR = -1,          // 通用错误
    INVALID_PARAM = -2,  // 无效参数
    NOT_INITIALIZED = -3,// 未初始化
    ALREADY_EXISTS = -4, // 已存在
    NOT_FOUND = -5,      // 未找到
    BUFFER_FULL = -6,    // 缓冲区满
    TIMEOUT = -7,        // 超时
    PERMISSION_DENIED = -8, // 权限拒绝
    OUT_OF_MEMORY = -9   // 内存耗尽
};
```

#### 工具函数

```cpp
// 构造数据点 key (地址 + ID)
uint64_t makeKey(int addr, int id);
// 示例：makeKey(1, 5) -> 设备 1 的第 5 号点位

// 从 key 提取地址
int getKeyAddr(uint64_t key);

// 从 key 提取 ID
int getKeyId(uint64_t key);

// 获取当前时间戳 (毫秒)
uint64_t getCurrentTimestamp();
```

#### Event 结构体

事件结构体，用于跨进程事件传递，固定 64 字节：

```cpp
struct Event {
    uint64_t key;               // 数据点 key (设备地址 << 32 | 点号)
    int32_t addr;               // 设备地址
    int32_t id;                 // 数据点 ID
    PointType pointType;        // 点位类型 (YX/YC/DZ/YK)
    uint8_t reserved;           // 保留对齐
    
    union oldValue;             // 旧值
    union newValue;             // 新值
    
    uint64_t timestamp;         // 事件时间戳 (毫秒)
    uint8_t quality;            // 质量码
    uint8_t isCritical;         // 是否关键点
    uint16_t sourcePid;         // 来源进程 PID
    char source[16];            // 来源标识
};

// 值联合体用法：
// YX/YK: event.oldValue.intValue / event.newValue.intValue
// YC/DZ: event.oldValue.floatValue / event.newValue.floatValue
```

#### SOERecord 结构体

SOE 事件记录结构体，纳秒级时标，固定 32 字节：

```cpp
struct SOERecord {
    uint64_t absoluteTime;      // 绝对时标 (纳秒，Unix时间)
    uint64_t monotonicTime;     // 单调时钟时标 (纳秒)
    uint32_t pointKey;          // 点位标识 (设备地址 << 16 | 点号)
    uint16_t msoc;              // 毫秒计数器 (0-999)
    uint16_t sourcePid;         // 来源进程 PID
    uint8_t pointType;          // 点位类型 (PointType)
    uint8_t eventType;          // 事件类型 (SOEEventType)
    uint8_t quality;            // 质量码 (SOEQuality)
    uint8_t oldValue;           // 旧值
    uint8_t newValue;           // 新值
    uint8_t priority;           // 优先级 (0-255，越高越优先)
};
```

#### SOEEventType 枚举

SOE 事件类型：

```cpp
enum class SOEEventType : uint8_t {
    YX_CHANGE = 0,          // 遥信变位
    YK_EXECUTE = 1,         // 遥控执行
    YK_FEEDBACK = 2,        // 遥控返校
    ALARM_TRIGGER = 3,      // 告警触发
    ALARM_CLEAR = 4,        // 告警复归
    PROTECTION_ACT = 5,     // 保护动作
    FAULT_RECORD = 6        // 故障录波触发
};
```

#### SOEQuality 枚举

SOE 事件质量码：

```cpp
enum class SOEQuality : uint8_t {
    VALID = 0,              // 有效
    INVALID = 1,            // 无效
    QUESTIONABLE = 2,       // 可疑
    RESERVE_OVERFLOW = 3    // 缓冲区溢出
};
```

#### ProcessHealth 枚举

进程健康状态：

```cpp
enum class ProcessHealth : uint8_t {
    HEALTHY = 0,        // 健康 (心跳正常)
    WARNING = 1,        // 警告 (心跳延迟)
    DEAD = 2,           // 死亡 (心跳超时)
    UNKNOWN = 3         // 未知 (未注册)
};
```

#### DataPoolStats 结构体

数据池统计信息：

```cpp
struct DataPoolStats {
    uint64_t totalReads;        // 总读取次数
    uint64_t totalWrites;       // 总写入次数
    uint64_t yxWrites;          // YX 写入次数
    uint64_t ycWrites;          // YC 写入次数
    uint64_t dzWrites;          // DZ 写入次数
    uint64_t ykWrites;          // YK 写入次数
    uint64_t eventPublishes;    // 事件发布次数
    uint64_t eventProcesses;    // 事件处理次数
    uint64_t lastResetTime;     // 上次重置时间
    uint32_t activeProcessCount;// 活跃进程数
};
```

#### SOEQueryCondition 结构体

SOE 查询条件：

```cpp
struct SOEQueryCondition {
    uint64_t startTime;         // 开始时间 (纳秒，0表示不限制)
    uint64_t endTime;           // 结束时间 (纳秒，0表示不限制)
    uint32_t pointKey;          // 点位标识 (0表示全部)
    uint8_t pointType;          // 点位类型 (0xFF表示全部)
    uint8_t eventType;          // 事件类型 (0xFF表示全部)
    uint8_t minPriority;        // 最低优先级 (0表示全部)
    uint32_t maxRecords;        // 最大返回记录数
    bool reverseOrder;          // 是否逆序 (最新在前)
};

// 使用示例
SOEQueryCondition cond;
cond.startTime = startNs;
cond.endTime = endNs;
cond.pointType = static_cast<uint8_t>(PointType::YX);
cond.maxRecords = 100;
cond.reverseOrder = true;
```

#### SOEStats 结构体

SOE 统计信息：

```cpp
struct SOEStats {
    uint64_t totalRecords;      // 总记录数
    uint64_t droppedRecords;    // 丢弃记录数
    uint64_t lastRecordTime;    // 最后记录时间
    uint32_t currentLoad;       // 当前缓冲区使用量
    float loadPercent;          // 缓冲区使用率
    uint32_t capacity;          // 缓冲区容量
};
```

#### 质量码定义

数据点的 `quality` 字段用于表示数据的有效性：

| 值 | 含义 | 说明 |
|:--:|------|------|
| 0 | 正常 | 数据有效，质量良好 |
| 1 | 无效 | 数据无效，传感器故障或通信中断 |
| 2 | 可疑 | 数据可疑，需要进一步确认 |
| 3 | 维护 | 设备处于维护状态 |
| 4-127 | 保留 | 预留扩展 |
| 128-255 | 自定义 | 用户自定义质量码 |

**常见使用场景**：

```cpp
// 正常数据
client->setYX(key, 1, 0);

// 通信中断，数据无效
client->setYX(key, 0, 1);

// 设备检修中
client->setYX(key, 0, 3);
```

### 5.2 DataPoolClient 类

#### 初始化与销毁

```cpp
class DataPoolClient {
public:
    struct Config {
        std::string poolName;       // 数据池共享内存名称
        std::string eventName;      // 事件中心名称
        std::string soeName;        // SOE 记录器名称
        std::string processName;    // 进程名称
        uint32_t yxCount;           // YX 点数
        uint32_t ycCount;           // YC 点数
        uint32_t dzCount;           // DZ 点数
        uint32_t ykCount;           // YK 点数
        uint32_t eventCapacity;     // 事件缓冲容量
        uint32_t soeCapacity;       // SOE 缓冲容量
        bool create;                // true=创建，false=连接
        bool enablePersistence;     // 启用持久化
        bool enableSOE;             // 启用 SOE 记录
        bool enableVoting;          // 启用表决引擎
        bool enableIEC61850;        // 启用 IEC61850 映射
        
        PersistentConfig persistConfig;      // 持久化配置
        VotingEngine::ShmConfig votingConfig;// 表决配置
        IEC61850Mapper::Config iec61850Config;// IEC61850 配置
    };
    
    // 创建或连接客户端
    static DataPoolClient* init(const Config& config);
    
    // 关闭客户端
    void shutdown();
};
```

#### 数据操作 API

##### YX (遥信) 操作

```cpp
// 设置遥信值
bool setYX(uint64_t key, uint8_t value, uint8_t quality = 0);
// 参数:
//   key: 点位标识 (使用 makeKey 构造)
//   value: 值 (0=分位，1=合位)
//   quality: 质量码 (0=有效)
// 返回：true=成功

// 获取遥信值
bool getYX(uint64_t key, uint8_t& value, uint8_t& quality);
// 参数:
//   key: 点位标识
//   value: 输出值
//   quality: 输出质量码
// 返回：true=成功

// 通过索引设置 (高性能)
bool setYXByIndex(uint32_t index, uint8_t value, uint8_t quality = 0);

// 通过索引获取 (高性能)
bool getYXByIndex(uint32_t index, uint8_t& value, uint8_t& quality);
```

##### YC (遥测) 操作

```cpp
// 设置遥测值
bool setYC(uint64_t key, float value, uint8_t quality = 0);

// 获取遥测值
bool getYC(uint64_t key, float& value, uint8_t& quality);

// 通过索引操作
bool setYCByIndex(uint32_t index, float value, uint8_t quality = 0);
bool getYCByIndex(uint32_t index, float& value, uint8_t& quality);
```

##### DZ (定值) 操作

```cpp
// 设置定值
bool setDZ(uint64_t key, float value, uint8_t quality = 0);

// 获取定值
bool getDZ(uint64_t key, float& value, uint8_t& quality);
```

##### YK (遥控) 操作

```cpp
// 设置遥控
bool setYK(uint64_t key, uint8_t value, uint8_t quality = 0);

// 获取遥控
bool getYK(uint64_t key, uint8_t& value, uint8_t& quality);
```

#### 点位注册

```cpp
// 注册数据点
bool registerPoint(uint64_t key, PointType type, uint32_t& index);
// 参数:
//   key: 点位标识
//   type: 点位类型
//   index: 输出的索引值 (用于后续快速访问)
// 返回：true=成功

// 查找数据点
bool findPoint(uint64_t key, PointType& type, uint32_t& index);
```

#### 事件操作

```cpp
// 发布数据变更事件
bool publishEvent(uint64_t key, PointType type, 
                  uint32_t oldValue, uint32_t newValue);
bool publishEvent(uint64_t key, PointType type, 
                  float oldValue, float newValue);

// 订阅事件
uint32_t subscribe(std::function<void(const Event&)> callback);
// 参数:
//   callback: 事件回调函数
// 返回：订阅者 ID (INVALID_INDEX=失败)

// 取消订阅
bool unsubscribe(uint32_t subscriberId);

// 处理待处理事件
uint32_t processEvents(uint32_t subscriberId, uint32_t maxEvents = 0);
// 参数:
//   subscriberId: 订阅者 ID
//   maxEvents: 最大处理数量 (0=无限制)
// 返回：处理的事件数

// 拉取单个事件
bool pollEvent(uint32_t subscriberId, Event& event);
```

#### 便捷方法

```cpp
// 设置 YX 并发布事件
bool setYXWithEvent(uint64_t key, uint8_t value, uint8_t quality = 0);

// 设置 YC 并发布事件
bool setYCWithEvent(uint64_t key, float value, uint8_t quality = 0);
```

#### 状态查询

```cpp
// 检查客户端是否有效
bool isValid() const;

// 是否为创建者
bool isCreator() const;

// 获取进程名称
const std::string& getProcessName() const;

// 获取进程 ID
uint32_t getProcessId() const;

// 获取数据池对象
SharedDataPool* getDataPool();

// 获取事件中心对象
IPCEventCenter* getEventCenter();
```

### 5.2.1 表决引擎结构体

#### VotingResult 枚举

表决结果类型：

```cpp
enum class VotingResult : uint8_t {
    UNANIMOUS = 0,      // 一致 (三源相同)
    MAJORITY = 1,       // 多数 (二取一)
    DISAGREE = 2,       // 不一致 (三源各不相同)
    INSUFFICIENT = 3,   // 有效源不足
    FAILED = 4          // 表决失败
};
```

#### SourceStatus 枚举

表决源状态：

```cpp
enum class SourceStatus : uint8_t {
    VALID = 0,          // 有效
    INVALID = 1,        // 无效 (质量码坏)
    TIMEOUT = 2,        // 超时
    OUT_OF_RANGE = 3,   // 超出范围
    DISCONNECTED = 4    // 断开
};
```

#### DeviationLevel 枚举

偏差告警级别：

```cpp
enum class DeviationLevel : uint8_t {
    NONE = 0,           // 无偏差
    MINOR = 1,          // 轻微偏差
    MODERATE = 2,       // 中等偏差
    SEVERE = 3          // 严重偏差
};
```

#### VotingConfig 结构体

表决配置，用于定义一个三取二表决组：

```cpp
struct VotingConfig {
    uint32_t groupId;           // 表决组 ID
    char name[32];              // 表决组名称
    uint64_t sourceKeyA;        // 源 A 的 key
    uint64_t sourceKeyB;        // 源 B 的 key
    uint64_t sourceKeyC;        // 源 C 的 key
    uint8_t sourceType;         // 源类型 (0=YX, 1=YC)
    uint8_t votingStrategy;     // 表决策略 (见下表)
    uint8_t prioritySource;     // 优先源 (策略 2 时有效) 0=A, 1=B, 2=C
    uint8_t enableDeviation;    // 是否启用偏差检测
    float deviationLimit;       // 偏差限值 (YC 用)
    uint8_t deviationCountLimit;// 偏差计数限值
    uint32_t timeoutMs;         // 超时时间 (毫秒)
    uint32_t alarmMask;         // 告警屏蔽字
};
```

**表决策略说明**：

| 策略值 | 名称 | 说明 |
|:------:|------|------|
| 0 | 严格三取二 | 必须三个源都有效，且至少两个一致 |
| 1 | 宽松表决 | 任意两个源一致即可，允许一个源无效 |
| 2 | 优先级表决 | 按优先源顺序选择，优先源有效则使用其值 |

#### VotingOutput 结构体

表决输出结果：

```cpp
struct VotingOutput {
    uint32_t groupId;           // 表决组 ID
    uint8_t result;             // VotingResult
    uint8_t deviationLevel;     // DeviationLevel
    uint8_t validSourceCount;   // 有效源数量
    
    union {
        uint8_t yxValue;        // 表决后的 YX 值
        float ycValue;          // 表决后的 YC 值
        uint32_t rawValue;      // 原始值
    };
    
    uint8_t quality;            // 输出质量码
    uint8_t deviationCount;     // 连续偏差计数
    uint16_t alarmFlags;        // 告警标志
    
    SourceData sources[3];      // 三个源的原始数据
};
```

**告警标志位说明**：

| 位 | 含义 |
|:--:|------|
| 0 | 源 A 偏差 |
| 1 | 源 B 偏差 |
| 2 | 源 C 偏差 |
| 3 | 源 A 超时 |
| 4 | 源 B 超时 |
| 5 | 源 C 超时 |
| 6 | 有效源不足 |
| 7 | 表决失败 |

#### VotingStats 结构体

表决统计信息：

```cpp
struct VotingStats {
    uint32_t totalVotes;        // 总表决次数
    uint32_t unanimousCount;    // 一致次数
    uint32_t majorityCount;     // 多数次
    uint32_t disagreeCount;     // 不一致次数
    uint32_t insufficientCount;// 有效源不足次数
    uint32_t deviationAlarms;   // 偏差告警次数
    uint64_t lastVotingTime;    // 最后表决时间
};
```

### 5.3 SharedDataPool 类 (底层 API)

#### 创建与连接

```cpp
class SharedDataPool {
public:
    // 创建数据池
    static SharedDataPool* create(const char* name,
                                   uint32_t yxCount,
                                   uint32_t ycCount,
                                   uint32_t dzCount,
                                   uint32_t ykCount);
    
    // 连接已有数据池
    static SharedDataPool* connect(const char* name);
    
    // 销毁数据池
    void destroy();
    
    // 断开连接 (不销毁)
    void disconnect();
};
```

#### 批量操作

```cpp
// 批量设置 YX
Result batchSetYX(const uint32_t* indices, const uint8_t* values,
                  uint32_t count, uint32_t& successCount);

// 批量设置 YC
Result batchSetYC(const uint32_t* indices, const float* values,
                  uint32_t count, uint32_t& successCount);

// 批量获取 YX
Result batchGetYX(const uint32_t* indices, uint8_t* values,
                  uint32_t count, uint32_t& successCount);

// 批量获取 YC
Result batchGetYC(const uint32_t* indices, float* values,
                  uint32_t count, uint32_t& successCount);
```

#### 统计信息

```cpp
struct DataPoolStats {
    uint64_t totalReadOps;    // 总读取次数
    uint64_t totalWriteOps;   // 总写入次数
    uint64_t yxWriteCount;    // YX 写入次数
    uint64_t ycWriteCount;    // YC 写入次数
    uint64_t dzWriteCount;    // DZ 写入次数
    uint64_t ykWriteCount;    // YK 写入次数
};

// 获取统计信息
DataPoolStats getStats() const;

// 重置统计
void resetStats();
```

### 5.4 IPCEventCenter 类

#### 创建与销毁

```cpp
class IPCEventCenter {
public:
    // 创建事件中心
    static IPCEventCenter* create(const char* name, 
                                   uint32_t eventCapacity);
    
    // 连接事件中心
    static IPCEventCenter* connect(const char* name);
    
    // 销毁
    void destroy();
};
```

#### 发布/订阅

```cpp
// 发布事件
Result publishEvent(const Event& event);

// 订阅事件
Result subscribe(uint32_t& subscriberId, 
                 std::function<void(const Event&)> callback);

// 取消订阅
Result unsubscribe(uint32_t subscriberId);

// 处理事件
Result processEvents(uint32_t subscriberId, uint32_t maxCount);
```

### 5.5 SOERecorder 类

#### 创建与连接

```cpp
class SOERecorder {
public:
    // 创建 SOE 记录器
    static SOERecorder* create(const char* name, 
                                uint32_t capacity = 100000);
    
    // 连接 SOE 记录器
    static SOERecorder* connect(const char* name);
    
    // 销毁
    void destroy();
};
```

#### 记录操作

```cpp
// 记录 SOE 事件
Result record(const SOERecord& record);

// 记录遥信变位
Result recordYXChange(uint32_t pointKey, 
                      uint8_t oldValue, uint8_t newValue,
                      uint8_t priority = 128);

// 记录遥控执行
Result recordYKExecute(uint32_t pointKey, 
                       uint8_t command, uint8_t priority = 200);

// 记录保护动作
Result recordProtectionAct(uint32_t pointKey, 
                           uint8_t action, uint8_t priority = 255);
```

#### 查询操作

```cpp
// 查询记录
Result query(const SOEQueryCondition& condition,
             SOERecord* records, uint32_t& count, uint32_t maxCount);

// 获取最新 N 条记录
Result getLatest(uint32_t count, SOERecord* records, 
                 uint32_t& actualCount);

// 按时间范围查询
Result getByTimeRange(uint64_t startTime, uint64_t endTime,
                      SOERecord* records, uint32_t& count, 
                      uint32_t maxCount);
```

#### 导出功能

```cpp
// 导出为 CSV
Result exportToCSV(const char* filename, 
                   const SOEQueryCondition& condition);

// 导出为 COMTRADE
Result exportToCOMTRADE(const char* cfgFile, const char* datFile,
                        const SOEQueryCondition& condition);
```

---

## 6. 高级功能

### 6.1 持久化存储

#### 配置参数

```cpp
struct PersistentConfig {
    std::string snapshotDir;      // 快照目录
    uint32_t autoSnapshotInterval;// 自动快照间隔 (秒)
    uint32_t maxBackups;          // 最大备份数
    uint32_t flags;               // 标志位
    
    PersistentConfig()
        : snapshotDir("/var/lib/ipc_snapshots"),
          autoSnapshotInterval(300),  // 5 分钟
          maxBackups(10),
          flags(PERSIST_DEFAULT) {}
};
```

#### 使用示例

```cpp
DataPoolClient::Config config;
config.enablePersistence = true;
config.persistConfig.snapshotDir = "/data/ipc_backup";
config.persistConfig.autoSnapshotInterval = 600;  // 10 分钟
config.persistConfig.maxBackups = 5;

DataPoolClient* client = DataPoolClient::init(config);

// 手动保存快照
if (client->saveSnapshot("emergency_backup")) {
    std::cout << "快照保存成功!" << std::endl;
}

// 恢复快照
if (client->loadSnapshot("emergency_backup")) {
    std::cout << "快照恢复成功!" << std::endl;
}
```

### 6.2 三取二表决

#### 配置参数

```cpp
struct VotingConfig {
    uint32_t groupId;         // 表决组 ID
    uint32_t channelCount;    // 通道数 (通常=3)
    uint64_t inputKeys[3];    // 输入点位 key
    uint64_t outputKey;       // 输出点位 key
    float deviationThreshold; // 偏差阈值 (YC 用)
};
```

#### 使用示例

```cpp
// 启用表决引擎
config.enableVoting = true;
config.votingConfig.shmName = "/ipc_voting";
config.votingConfig.maxGroups = 50;

DataPoolClient* client = DataPoolClient::init(config);

// 添加表决组
VotingConfig voteCfg;
voteCfg.groupId = 1;
voteCfg.channelCount = 3;
voteCfg.inputKeys[0] = makeKey(10, 0);  // 通道 A
voteCfg.inputKeys[1] = makeKey(10, 1);  // 通道 B
voteCfg.inputKeys[2] = makeKey(10, 2);  // 通道 C
voteCfg.outputKey = makeKey(10, 99);    // 表决结果
voteCfg.deviationThreshold = 0.5f;      // 0.5V 偏差阈值

client->addVotingGroup(voteCfg);

// 执行表决
uint8_t result;
if (client->executeVoting(1, result)) {
    std::cout << "表决结果：" << (int)result << std::endl;
}
```

### 6.3 IEC 61850 映射

#### 配置参数

```cpp
struct DAMapping {
    std::string logicalNode;    // 逻辑节点名 (如"GGIO")
    std::string dataObject;     // 数据对象名 (如"Ind")
    std::string dataAttribute;  // 数据属性名 (如"stVal")
    uint32_t dataKey;           // 关联的数据点 key
    uint8_t pointType;          // 点位类型
};
```

#### 使用示例

```cpp
// 启用 IEC61850 映射
config.enableIEC61850 = true;
config.iec61850Config.shmName = "/iec61850_mapper";
config.iec61850Config.maxMappings = 1000;
config.iec61850Config.maxLogicalNodes = 100;

DataPoolClient* client = DataPoolClient::init(config);

// 添加数据映射
DAMapping mapping;
mapping.logicalNode = "GGIO";
mapping.dataObject = "Ind";
mapping.dataAttribute = "stVal";
mapping.dataKey = makeKey(1, 0);
mapping.pointType = static_cast<uint8_t>(PointType::YX);

uint32_t mappingId = client->addDAMapping(mapping);

// 解析 SCL 文件
if (client->parseSCLFile("substation.scl")) {
    std::cout << "SCL 文件解析成功!" << std::endl;
}

// 导出数据集
client->exportDataSet("MyDataSet", {mappingId1, mappingId2});
```

### 6.4 进程管理与健康监控

#### 进程注册

```cpp
// 注册当前进程
uint32_t processId;
client->registerProcess("DataAcquisition", processId);

// 更新心跳
client->updateHeartbeat();

// 注销进程
client->unregisterProcess(processId);
```

#### 健康检查

```cpp
enum class ProcessHealth {
    HEALTHY,      // 健康
    UNHEALTHY,    // 不健康 (心跳超时)
    DEAD          // 已死亡
};

// 检查进程健康状态
ProcessHealth health = client->checkProcessHealth(processId);

// 获取活跃进程列表
std::vector<uint32_t> activeProcesses;
client->getActiveProcessList(activeProcesses);

// 清理死亡进程
uint32_t cleanedCount = client->cleanupDeadProcesses();
```

---

## 7. 最佳实践

### 7.1 性能优化

#### 使用索引访问代替 Key 访问

```cpp
// ❌ 慢：每次查找都需要计算哈希
for (int i = 0; i < 1000; i++) {
    client->setYX(makeKey(1, i), value);
}

// ✅ 快：预先注册，使用索引
std::vector<uint32_t> indices(1000);
for (int i = 0; i < 1000; i++) {
    client->registerPoint(makeKey(1, i), PointType::YX, indices[i]);
}

for (int i = 0; i < 1000; i++) {
    client->setYXByIndex(indices[i], value);  // O(1) 访问
}
```

#### 批量操作

```cpp
// ❌ 单次操作开销大
for (int i = 0; i < 100; i++) {
    client->setYXByIndex(i, value[i]);
}

// ✅ 批量操作，减少锁竞争
std::vector<uint32_t> indices(100);
std::vector<uint8_t> values(100);
// ... 填充数据
uint32_t successCount;
client->getDataPool()->batchSetYX(indices.data(), values.data(), 
                                   100, successCount);
```

#### 减少事件发布频率

```cpp
// ❌ 频繁发布事件
for (int i = 0; i < 1000; i++) {
    client->setYXWithEvent(key, value);  // 每次都发布事件
}

// ✅ 批量更新后发布一次
for (int i = 0; i < 1000; i++) {
    client->setYX(key, value);  // 不发布事件
}
client->publishEvent(key, PointType::YX, oldValue, newValue);  // 发布一次
```

### 7.2 线程安全

#### 多线程并发写入

```cpp
// ✅ 线程安全：内部已实现读写锁保护
std::vector<std::thread> workers;
for (int t = 0; t < 4; t++) {
    workers.emplace_back([&client, t]() {
        for (int i = 0; i < 1000; i++) {
            client->setYCByIndex(t * 1000 + i, static_cast<float>(i));
        }
    });
}
for (auto& w : workers) w.join();
```

#### 避免长时间持有锁

```cpp
// ❌ 在锁内执行耗时操作
pool->lockWrite();
for (int i = 0; i < 1000000; i++) {
    // 耗时计算...
}
pool->unlockWrite();

// ✅ 先准备数据，快速写入
std::vector<float> tempData(1000000);
// 耗时计算在锁外完成...

pool->lockWrite();
memcpy(dataPtr, tempData.data(), sizeof(float) * 1000000);
pool->unlockWrite();
```

### 7.3 错误处理

#### 完整的错误检查

```cpp
DataPoolClient* client = DataPoolClient::init(config);
if (!client) {
    std::cerr << "初始化失败" << std::endl;
    return -1;
}

if (!client->isValid()) {
    std::cerr << "客户端无效" << std::endl;
    delete client;
    return -1;
}

uint8_t value;
uint8_t quality;
if (!client->getYX(key, value, quality)) {
    std::cerr << "读取失败，点位可能不存在" << std::endl;
    // 尝试注册点位
    uint32_t index;
    if (client->registerPoint(key, PointType::YX, index)) {
        std::cout << "点位注册成功，索引=" << index << std::endl;
    }
}
```

#### 异常安全

```cpp
DataPoolClient* client = nullptr;
try {
    client = DataPoolClient::init(config);
    if (!client) throw std::runtime_error("初始化失败");
    
    // 使用 client...
    
} catch (const std::exception& e) {
    std::cerr << "异常：" << e.what() << std::endl;
    if (client) {
        client->shutdown();
        delete client;
    }
    throw;
}
```

### 7.4 资源管理

#### 正确的清理顺序

```cpp
// ✅ 正确的销毁顺序
~Application() {
    // 1. 停止数据更新
    m_timer->stop();
    
    // 2. 取消订阅
    if (m_subscriberId != INVALID_INDEX) {
        m_client->unsubscribe(m_subscriberId);
    }
    
    // 3. 关闭客户端
    if (m_client) {
        m_client->shutdown();
        delete m_client;
    }
    
    // 4. 清理共享内存 (仅创建者)
    if (m_isCreator) {
        shm_unlink("/ipc_data_pool");
        shm_unlink("/ipc_events");
    }
}
```

#### 防止内存泄漏

```cpp
// ✅ 使用智能指针管理
class DataManager {
    std::unique_ptr<DataPoolClient> m_client;
    
public:
    DataManager(const Config& cfg) {
        m_client.reset(DataPoolClient::init(cfg));
        if (!m_client) {
            throw std::runtime_error("初始化失败");
        }
    }
    
    ~DataManager() {
        // unique_ptr 会自动调用删除器
        // 但需要先调用 shutdown()
        if (m_client) {
            m_client->shutdown();
        }
    }
};
```

### 7.5 调试技巧

#### 启用详细日志

```cpp
// 在 Common.h 中定义调试宏
#ifdef DEBUG
#define IPC_DEBUG(fmt, ...) \
    printf("[DEBUG][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_DEBUG(...) do {} while(0)
#endif

// 使用时
IPC_DEBUG("设置 YX: key=%lu, value=%d", key, value);
```

#### 共享内存检查

```bash
# 查看共享内存状态
ls -lh /dev/shm/ipc_*

# 查看共享内存内容 (十六进制)
hexdump -C /dev/shm/ipc_data_pool | head -20

# 删除共享内存
rm -f /dev/shm/ipc_data_pool
```

#### 性能分析

```cpp
// 使用 chrono 测量耗时
auto start = std::chrono::high_resolution_clock::now();

for (int i = 0; i < 10000; i++) {
    client->setYXByIndex(i % 1000, 1);
}

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

std::cout << "10000 次写入耗时：" << duration.count() << "μs" 
          << ", 平均：" << duration.count() / 10000.0 << "μs/次" << std::endl;
```

---

## 8. 故障排除

### 8.1 常见问题

#### Q1: 初始化失败，返回 nullptr

**可能原因**:
- 共享内存已存在且未正确清理
- 权限不足
- 内存不足

**解决方法**:
```bash
# 清理共享内存
rm -f /dev/shm/ipc_*

# 检查权限
ls -l /dev/shm/

# 检查内存
free -h
```

#### Q2: 跨进程数据不同步

**可能原因**:
- 未正确使用读写锁
- 进程崩溃导致锁未释放
- 内存屏障缺失

**解决方法**:
```cpp
// 确保成对使用锁
pool->lockWrite();
// ... 修改数据
pool->unlockWrite();

// 如果锁死，重启创建进程解锁
```

#### Q3: 事件丢失

**可能原因**:
- 事件缓冲区满
- 订阅者处理速度过慢
- 未及时调用 processEvents

**解决方法**:
```cpp
// 增大事件缓冲区
config.eventCapacity = 50000;  // 默认 10000

// 提高事件处理频率
while (running) {
    client->processEvents(subId, 1000);  // 增加单次处理量
    usleep(1000);  // 缩短间隔
}
```

#### Q4: SOE 记录不准确

**可能原因**:
- 系统时钟不同步
- 记录时标未使用单调时钟
- 缓冲区溢出

**解决方法**:
```cpp
// 使用单调时钟
clock_gettime(CLOCK_MONOTONIC, &ts);

// 检查缓冲区使用率
SOEStats stats = recorder->getStats();
if (stats.loadPercent > 80.0f) {
    std::cerr << "警告：SOE 缓冲区即将溢出!" << std::endl;
}
```

### 8.2 调试工具

#### Valgrind 内存检查

```bash
# 检测内存泄漏
valgrind --leak-check=full --show-leak-kinds=all \
    ./myapp

# 检测线程问题
valgrind --tool=helgrind ./myapp
```

#### strace 系统调用跟踪

```bash
# 跟踪共享内存相关调用
strace -e trace=memory,mmap ./myapp

# 跟踪所有系统调用
strace -f -o trace.log ./myapp
```

#### GDB 调试

```bash
# 附加到运行中的进程
gdb -p <pid>

# 查看共享内存映射
(gdb) info proc mappings
```

### 8.3 性能调优

#### 调整共享内存大小

```cpp
// 根据实际需求设置点数，避免浪费
config.yxCount = 实际需要的 YX 点数;  // 不要盲目设为最大值

// 计算内存占用
内存 ≈ (yxCount * 10) + (ycCount * 13) + 
       (索引表大小) + (进程信息)
```

#### 优化锁粒度

```cpp
// 当前实现：全局读写锁
// 优化方向：按数据类型分锁
// YX/YC/DZ/YK 各自独立锁，提高并发度
```

#### NUMA 感知

```cpp
// 在 NUMA 系统上，绑定到本地节点
numactl --cpunodebind=0 --membind=0 ./myapp
```

---

## 附录

### A. 示例代码索引

- [简单数据读写](#43-第一个程序创建数据池)
- [多进程通信](#44-多进程通信示例)
- [事件订阅发布](#52-datapoolclient-类)
- [SOE 记录](#63-soerecorder-类)
- [持久化存储](#61-持久化存储)
- [三取二表决](#62-三取二表决)

### B. 头文件位置

```
IPCSharedDataPool/include/
├── Common.h              # 基础类型定义
├── DataPoolClient.h      # 推荐使用的高层 API
├── SharedDataPool.h      # 底层数据池 API
├── IPCEventCenter.h      # 事件中心 API
├── SOERecorder.h         # SOE 记录器 API
├── PersistentStorage.h   # 持久化存储 API
├── VotingEngine.h        # 表决引擎 API
├── IEC61850Mapper.h      # IEC61850 映射 API
├── ProcessRWLock.h       # 跨进程锁
└── ShmRingBuffer.h       # 共享内存环形缓冲
```

### C. 许可证

MIT License - 详见项目 LICENSE 文件

### D. 联系方式

- 项目主页：[GitHub Repository]
- 问题反馈：[Issue Tracker]
- 技术讨论：[Discussion Forum]

---

**版本**: 1.0  
**更新日期**: 2026-03-05  
**文档状态**: 完整版
