/**
 * @file tst_process_monitor.cpp
 * @brief 进程监控测试
 */

#include "../include/ProcessMonitor.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cmath>

using namespace IPC;

static int s_passed = 0;
static int s_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED" << std::endl; \
        s_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << std::endl; \
        s_failed++; \
    } catch (...) { \
        std::cout << "FAILED: unknown error" << std::endl; \
        s_failed++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

// ========== 测试用例 ==========

TEST(construction) {
    // 默认构造
    ProcessMonitor monitor1;
    ASSERT_TRUE(monitor1.getPid() > 0);
    ASSERT_TRUE(monitor1.getName().length() > 0);

    // 指定PID构造
    ProcessMonitor monitor2(getpid());
    ASSERT_EQ(monitor2.getPid(), getpid());
}

TEST(get_process_info) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 验证基本字段
    ASSERT_TRUE(info.pid > 0);
    ASSERT_TRUE(info.timestamp > 0);
    ASSERT_TRUE(info.threads > 0);
    ASSERT_TRUE(info.vmSize > 0);
    ASSERT_TRUE(info.vmRSS > 0);

    // 验证CPU时间
    ASSERT_TRUE(info.userTime >= 0);
    ASSERT_TRUE(info.systemTime >= 0);
    ASSERT_TRUE(info.totalTime == info.userTime + info.systemTime);
}

TEST(process_name) {
    ProcessMonitor monitor(getpid());
    const std::string& name = monitor.getName();

    // 进程名应该不为空
    ASSERT_TRUE(!name.empty());

    // 应该包含 "tst_process" 或类似的名称
    std::cout << "  Process name: " << name << std::endl;
}

TEST(memory_info) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 验证内存信息
    ASSERT_TRUE(info.vmSize > 0);
    ASSERT_TRUE(info.vmRSS > 0);
    ASSERT_TRUE(info.vmData > 0);
    ASSERT_TRUE(info.vmStk > 0);

    // 物理内存应该小于虚拟内存
    ASSERT_TRUE(info.vmRSS <= info.vmSize);

    // 内存使用率应该在合理范围内
    ASSERT_TRUE(info.memoryPercent >= 0 && info.memoryPercent <= 100);

    std::cout << "  Virtual memory: " << info.vmSize << " KB" << std::endl;
    std::cout << "  Physical memory: " << info.vmRSS << " KB" << std::endl;
}

TEST(cpu_info) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    // 第一次采样
    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 进行一些CPU密集型操作
    volatile int sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i;
    }

    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 第二次采样
    ProcessResourceInfo info2;
    ASSERT_TRUE(monitor.getProcessInfo(info2));

    // CPU时间应该增加
    ASSERT_TRUE(info2.totalTime >= info.totalTime);

    // 如果有CPU活动，使用率应该大于0
    // 注意：第一次采样可能无法计算CPU使用率
    if (info2.cpuPercentPerCore > 0) {
        std::cout << "  CPU usage: " << info2.cpuPercentPerCore << "%" << std::endl;
    }
}

TEST(fd_count) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 文件描述符数量应该合理
    ASSERT_TRUE(info.fdCount >= 3); // 至少有 stdin, stdout, stderr

    std::cout << "  FD count: " << info.fdCount << std::endl;
}

TEST(thread_count) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 线程数应该至少为1
    ASSERT_TRUE(info.threads >= 1);

    std::cout << "  Thread count: " << info.threads << std::endl;
}

TEST(get_system_info) {
    ProcessMonitor monitor;
    SystemResourceInfo info;

    ASSERT_TRUE(monitor.getSystemInfo(info));

    // 验证基本字段
    ASSERT_TRUE(info.cpuCores > 0);
    ASSERT_TRUE(info.totalMem > 0);
    ASSERT_TRUE(info.usedMem > 0);
    ASSERT_TRUE(info.uptime > 0);

    // 验证内存使用率
    ASSERT_TRUE(info.memoryUsage >= 0 && info.memoryUsage <= 100);

    // 验证负载平均值
    ASSERT_TRUE(info.loadAvg1 >= 0);
    ASSERT_TRUE(info.loadAvg5 >= 0);
    ASSERT_TRUE(info.loadAvg15 >= 0);

    // 负载平均值通常递增
    ASSERT_TRUE(info.loadAvg1 >= info.loadAvg5 && info.loadAvg5 >= info.loadAvg15);

    std::cout << "  CPU cores: " << info.cpuCores << std::endl;
    std::cout << "  Total memory: " << info.totalMem << " KB" << std::endl;
    std::cout << "  Memory usage: " << info.memoryUsage << "%" << std::endl;
    std::cout << "  Load average: " << info.loadAvg1 << ", " << info.loadAvg5 << ", " << info.loadAvg15 << std::endl;
}

TEST(system_cpu_usage) {
    ProcessMonitor monitor;
    SystemResourceInfo info;

    // 第一次采样
    ASSERT_TRUE(monitor.getSystemInfo(info));

    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 进行一些CPU操作
    volatile int sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 第二次采样
    SystemResourceInfo info2;
    ASSERT_TRUE(monitor.getSystemInfo(info2));

    // CPU使用率应该在合理范围内
    if (info2.cpuUsage > 0) {
        ASSERT_TRUE(info2.cpuUsage <= info2.cpuCores * 100);
        std::cout << "  System CPU usage: " << info2.cpuUsage << "%" << std::endl;
    }
}

TEST(shm_info) {
    // 测试静态方法
    ShmUsageInfo info = ProcessMonitor::getShmInfo("/test_shm", 1024 * 1024, 512 * 1024);

    ASSERT_EQ(info.name, "/test_shm");
    ASSERT_EQ(info.size, 1024u * 1024u);
    ASSERT_EQ(info.usedBytes, 512u * 1024u);
    ASSERT_EQ(info.usagePercent, 50.0);
}

TEST(format_memory) {
    // 测试内存格式化
    ASSERT_EQ(ProcessMonitor::formatMemory(512), "512 KB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1024), "1.0 MB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1536), "1.5 MB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1024 * 1024), "1.0 GB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1536 * 1024), "1.5 GB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1024 * 1024 * 1024), "1.0 TB");
}

TEST(timestamp_monotonic) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    ASSERT_TRUE(monitor.getProcessInfo(info));

    uint64_t ts1 = info.timestamp;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(monitor.getProcessInfo(info));

    uint64_t ts2 = info.timestamp;

    // 时间戳应该是单调递增的
    ASSERT_TRUE(ts2 > ts1);

    // 时间差应该接近100ms
    uint64_t diff = ts2 - ts1;
    ASSERT_TRUE(diff >= 90 && diff <= 200);
}

TEST(cpu_percent_calculation) {
    ProcessMonitor monitor;
    ProcessResourceInfo info;

    // 第一次采样，CPU使用率可能为0（第一次）
    ASSERT_TRUE(monitor.getProcessInfo(info));

    // 进行CPU密集型操作
    volatile int sum = 0;
    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count() < 200) {
        for (int i = 0; i < 10000; i++) {
            sum += i;
        }
    }

    // 第二次采样
    ProcessResourceInfo info2;
    ASSERT_TRUE(monitor.getProcessInfo(info2));

    // CPU使用率应该被计算出来
    if (info2.cpuPercentPerCore > 0) {
        // 单核使用率不应超过100%
        ASSERT_TRUE(info2.cpuPercentPerCore <= 100.0);

        // 总CPU使用率不应超过核心数 * 100%
        uint32_t cores = sysconf(_SC_NPROCESSORS_ONLN);
        ASSERT_TRUE(info2.cpuPercent <= cores * 100.0);

        std::cout << "  CPU percent: " << info2.cpuPercent << "%" << std::endl;
        std::cout << "  CPU per core: " << info2.cpuPercentPerCore << "%" << std::endl;
    }
}

TEST(monitor_different_pid) {
    // 监控init进程（PID 1）
    ProcessMonitor monitor(1);
    ProcessResourceInfo info;

    // 应该能获取到init进程的信息
    ASSERT_TRUE(monitor.getProcessInfo(info));
    ASSERT_EQ(info.pid, 1);
    ASSERT_TRUE(info.threads > 0);
}

TEST(system_info_consistency) {
    ProcessMonitor monitor;
    SystemResourceInfo info1, info2;

    ASSERT_TRUE(monitor.getSystemInfo(info1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(monitor.getSystemInfo(info2));

    // 系统信息大部分应该一致
    ASSERT_EQ(info1.cpuCores, info2.cpuCores);
    ASSERT_EQ(info1.totalMem, info2.totalMem);
    ASSERT_EQ(info1.totalSwap, info2.totalSwap);

    // 时间戳应该递增
    ASSERT_TRUE(info2.timestamp > info1.timestamp);

    // 负载平均值变化应该不大（短时间内）
    ASSERT_TRUE(std::abs(info2.loadAvg1 - info1.loadAvg1) < 1.0);
}

TEST(memory_format_edge_cases) {
    // 测试边界情况
    ASSERT_EQ(ProcessMonitor::formatMemory(0), "0 KB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1), "1 KB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1023), "1023 KB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1024), "1.0 MB");
    ASSERT_EQ(ProcessMonitor::formatMemory(1048575), "1024.0 MB"); // 1024*1024-1
    ASSERT_EQ(ProcessMonitor::formatMemory(1048576), "1.0 GB");
}

// ========== 并发测试 ==========

TEST(concurrent_monitoring) {
    const int THREAD_COUNT = 4;
    const int SAMPLING_COUNT = 100;

    std::atomic<int> errors(0);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([&, t]() {
            ProcessMonitor monitor;
            for (int i = 0; i < SAMPLING_COUNT; i++) {
                ProcessResourceInfo procInfo;
                SystemResourceInfo sysInfo;

                if (!monitor.getProcessInfo(procInfo)) {
                    errors++;
                }
                if (!monitor.getSystemInfo(sysInfo)) {
                    errors++;
                }

                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(errors.load(), 0);
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  ProcessMonitor Tests" << std::endl;
    std::cout << "====================================" << std::endl;

    RUN_TEST(construction);
    RUN_TEST(get_process_info);
    RUN_TEST(process_name);
    RUN_TEST(memory_info);
    RUN_TEST(cpu_info);
    RUN_TEST(fd_count);
    RUN_TEST(thread_count);
    RUN_TEST(get_system_info);
    RUN_TEST(system_cpu_usage);
    RUN_TEST(shm_info);
    RUN_TEST(format_memory);
    RUN_TEST(timestamp_monotonic);
    RUN_TEST(cpu_percent_calculation);
    RUN_TEST(monitor_different_pid);
    RUN_TEST(system_info_consistency);
    RUN_TEST(memory_format_edge_cases);
    RUN_TEST(concurrent_monitoring);

    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, "
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;

    return s_failed > 0 ? 1 : 0;
}
