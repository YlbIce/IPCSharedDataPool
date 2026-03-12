/**
 * @file tst_log_service.cpp
 * @brief 日志服务测试
 */

#ifdef HAS_LIBIEC61850

#include "../include/LogService.h"
#include "../include/Common.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

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

TEST(create_destroy) {
    LogService::LogConfig config;
    config.logName = "test_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = true;
    config.soeMaxEntries = 50;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);
    ASSERT_TRUE(logService->isInitialized());
    ASSERT_EQ(logService->getMaxEntries(), 100u);

    logService->destroy();
    delete logService;
}

TEST(log_event) {
    LogService::LogConfig config;
    config.logName = "event_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 创建一个简单的值
    MmsValue* value = MmsValue_newBoolean(true);
    ASSERT_TRUE(value != nullptr);

    // 记录事件日志
    bool result = logService->logEvent("event001", "LLN0$ST$Op",
                                       value, 0, getCurrentTimestamp(), "test");
    ASSERT_TRUE(result);

    // 查询日志
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].entryId, "event001");

    MmsValue_delete(value);
    logService->destroy();
    delete logService;
}

TEST(log_soe) {
    LogService::LogConfig config;
    config.logName = "soe_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = true;
    config.soeMaxEntries = 50;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录SOE事件
    bool result = logService->logSOE(makeKey(1, 100), 0, 1,
                                      getCurrentTimestamp(), 0);
    ASSERT_TRUE(result);

    // 记录另一个SOE事件
    result = logService->logSOE(makeKey(1, 101), 1, 0,
                                 getCurrentTimestamp(), 0);
    ASSERT_TRUE(result);

    // 查询日志
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 2u);

    // 验证统计信息
    LogServiceStats stats = logService->getStats();
    ASSERT_EQ(stats.soeCount, 2ull);

    logService->destroy();
    delete logService;
}

TEST(log_diagnostic) {
    LogService::LogConfig config;
    config.logName = "diag_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录诊断日志
    bool result = logService->logDiagnostic("diag001", "Test diagnostic message",
                                           getCurrentTimestamp());
    ASSERT_TRUE(result);

    // 查询日志
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 1u);

    logService->destroy();
    delete logService;
}

TEST(log_audit) {
    LogService::LogConfig config;
    config.logName = "audit_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录审计日志
    bool result = logService->logAudit("audit001", "Login",
                                      "admin", getCurrentTimestamp());
    ASSERT_TRUE(result);

    // 查询日志
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 1u);

    logService->destroy();
    delete logService;
}

TEST(query_logs) {
    LogService::LogConfig config;
    config.logName = "query_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录多条日志
    for (int i = 0; i < 10; i++) {
        MmsValue* value = MmsValue_newIntegerFromInt32(i);
        logService->logEvent("event" + std::to_string(i),
                            "LLN0$ST$Op", value, 0,
                            getCurrentTimestamp(), "test");
        MmsValue_delete(value);
    }

    // 查询所有日志
    std::vector<LogEntry> allEntries = logService->queryLogs();
    ASSERT_EQ(allEntries.size(), 10u);

    // 查询前5条
    std::vector<LogEntry> partialEntries = logService->queryLogs(0, 5);
    ASSERT_EQ(partialEntries.size(), 5u);

    // 查询后5条
    std::vector<LogEntry> lastEntries = logService->queryLogs(5, 5);
    ASSERT_EQ(lastEntries.size(), 5u);

    logService->destroy();
    delete logService;
}

TEST(clear_logs) {
    LogService::LogConfig config;
    config.logName = "clear_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录多条日志
    for (int i = 0; i < 10; i++) {
        logService->logDiagnostic("diag" + std::to_string(i),
                                 "message" + std::to_string(i),
                                 getCurrentTimestamp());
    }

    // 清空日志
    int cleared = logService->clearLogs();
    ASSERT_EQ(cleared, 10);

    // 验证已清空
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 0u);

    logService->destroy();
    delete logService;
}

TEST(clear_soe_logs) {
    LogService::LogConfig config;
    config.logName = "clear_soe_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = true;
    config.soeMaxEntries = 50;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录多条SOE事件
    for (int i = 0; i < 10; i++) {
        logService->logSOE(makeKey(1, i), i % 2, (i + 1) % 2,
                          getCurrentTimestamp(), 0);
    }

    // 清空SOE日志
    int cleared = logService->clearSOELogs();
    ASSERT_EQ(cleared, 10);

    // 验证统计信息
    LogServiceStats stats = logService->getStats();
    ASSERT_EQ(stats.soeCount, 0ull);

    logService->destroy();
    delete logService;
}

TEST(stats) {
    LogService::LogConfig config;
    config.logName = "stats_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = true;
    config.soeMaxEntries = 50;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 初始统计
    LogServiceStats stats = logService->getStats();
    ASSERT_EQ(stats.totalEntries, 0ull);
    ASSERT_EQ(stats.eventCount, 0ull);
    ASSERT_EQ(stats.soeCount, 0ull);

    // 记录事件
    MmsValue* value = MmsValue_newBoolean(true);
    logService->logEvent("event001", "LLN0$ST$Op", value, 0,
                        getCurrentTimestamp(), "test");
    MmsValue_delete(value);

    // 记录SOE
    logService->logSOE(makeKey(1, 100), 0, 1, getCurrentTimestamp(), 0);

    // 记录诊断
    logService->logDiagnostic("diag001", "message", getCurrentTimestamp());

    // 验证统计
    stats = logService->getStats();
    ASSERT_EQ(stats.totalEntries, 3ull);
    ASSERT_EQ(stats.eventCount, 1ull);
    ASSERT_EQ(stats.soeCount, 1ull);

    // 重置统计
    logService->resetStats();
    stats = logService->getStats();
    ASSERT_EQ(stats.totalEntries, 0ull);
    ASSERT_EQ(stats.currentEntries, 3u); // 当前条目数不变

    logService->destroy();
    delete logService;
}

TEST(callback) {
    LogService::LogConfig config;
    config.logName = "callback_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    std::atomic<int> callbackCount(0);

    // 设置回调
    logService->setLogCallback([&callbackCount](const LogEntry& entry) {
        callbackCount++;
    });

    // 记录日志
    logService->logDiagnostic("diag001", "message", getCurrentTimestamp());

    // 回调应该被触发
    ASSERT_EQ(callbackCount.load(), 1);

    logService->destroy();
    delete logService;
}

TEST(max_entries_limit) {
    LogService::LogConfig config;
    config.logName = "limit_log";
    config.lnRef = "test_ln";
    config.maxEntries = 5;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录超过最大条目数的日志
    for (int i = 0; i < 10; i++) {
        logService->logDiagnostic("diag" + std::to_string(i),
                                 "message" + std::to_string(i),
                                 getCurrentTimestamp());
    }

    // 验证只有最新的5条被保留
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), 5u);
    ASSERT_EQ(entries[0].entryId, "diag5");

    logService->destroy();
    delete logService;
}

TEST(unlimited_entries) {
    LogService::LogConfig config;
    config.logName = "unlimited_log";
    config.lnRef = "test_ln";
    config.maxEntries = 0; // 无限制
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录大量日志
    const int ENTRY_COUNT = 100;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        logService->logDiagnostic("diag" + std::to_string(i),
                                 "message" + std::to_string(i),
                                 getCurrentTimestamp());
    }

    // 验证所有日志都被保留
    std::vector<LogEntry> entries = logService->queryLogs();
    ASSERT_EQ(entries.size(), ENTRY_COUNT);

    logService->destroy();
    delete logService;
}

TEST(concurrent_logging) {
    LogService::LogConfig config;
    config.logName = "concurrent_log";
    config.lnRef = "test_ln";
    config.maxEntries = 1000;
    config.enableSOE = true;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    const int THREAD_COUNT = 4;
    const int LOGS_PER_THREAD = 100;
    std::atomic<int> errors(0);

    std::vector<std::thread> threads;
    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < LOGS_PER_THREAD; i++) {
                if (!logService->logDiagnostic("diag_" + std::to_string(t) + "_" + std::to_string(i),
                                             "message", getCurrentTimestamp())) {
                    errors++;
                }
                if (!logService->logSOE(makeKey(t, i), 0, 1,
                                       getCurrentTimestamp(), 0)) {
                    errors++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(errors.load(), 0);

    // 验证总日志数
    LogServiceStats stats = logService->getStats();
    ASSERT_EQ(stats.currentEntries, THREAD_COUNT * LOGS_PER_THREAD * 2);

    logService->destroy();
    delete logService;
}

TEST(timestamp_order) {
    LogService::LogConfig config;
    config.logName = "timestamp_log";
    config.lnRef = "test_ln";
    config.maxEntries = 100;
    config.enableSOE = false;

    LogService* logService = LogService::create(config);
    ASSERT_TRUE(logService != nullptr);

    // 记录多条日志
    for (int i = 0; i < 5; i++) {
        logService->logDiagnostic("diag" + std::to_string(i),
                                 "message" + std::to_string(i),
                                 getCurrentTimestamp());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 查询日志
    std::vector<LogEntry> entries = logService->queryLogs();

    // 验证时间戳顺序（应该是递增的）
    for (size_t i = 1; i < entries.size(); i++) {
        ASSERT_TRUE(entries[i].timestamp >= entries[i - 1].timestamp);
    }

    logService->destroy();
    delete logService;
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  LogService Tests" << std::endl;
    std::cout << "====================================" << std::endl;

    RUN_TEST(create_destroy);
    RUN_TEST(log_event);
    RUN_TEST(log_soe);
    RUN_TEST(log_diagnostic);
    RUN_TEST(log_audit);
    RUN_TEST(query_logs);
    RUN_TEST(clear_logs);
    RUN_TEST(clear_soe_logs);
    RUN_TEST(stats);
    RUN_TEST(callback);
    RUN_TEST(max_entries_limit);
    RUN_TEST(unlimited_entries);
    RUN_TEST(concurrent_logging);
    RUN_TEST(timestamp_order);

    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, "
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;

    return s_failed > 0 ? 1 : 0;
}

#else // !HAS_LIBIEC61850

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  LogService Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "SKIPPED: libiec61850 not available" << std::endl;
    std::cout << "====================================" << std::endl;
    return 0;
}

#endif // HAS_LIBIEC61850
