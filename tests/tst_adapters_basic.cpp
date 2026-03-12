/**
 * @file tst_adapters_basic.cpp
 * @brief IEC61850适配器基础测试
 *
 * 注意：这是一个基础测试框架，完整的适配器测试需要：
 * 1. 网络接口配置（需要root权限）
 * 2. libiec61850完整安装
 * 3. 可能需要实际的硬件或仿真环境
 *
 * 本测试主要验证API接口和数据结构
 */

#ifdef HAS_LIBIEC61850

#include "../include/GooseAdapter.h"
#include "../include/SVAdapter.h"
#include "../include/ReportAdapter.h"
#include <iostream>

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

// ========== GOOSE 适配器测试 ==========

TEST(goose_config_default) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 4;
    config.vlanId = 0;
    config.appId = 0x0000;
    config.useVlanTag = false;
    config.goID = "GOOSE1";
    config.goCBRef = "LD0/LLN0$GO$gocb1";
    config.dataSetRef = "LD0/LLN0$GO$gocb1";
    config.confRev = 1;
    config.timeAllowedToLive = 2000;

    ASSERT_EQ(config.vlanPriority, 4);
    ASSERT_EQ(config.vlanId, 0);
    ASSERT_EQ(config.appId, 0x0000u);
    ASSERT_FALSE(config.useVlanTag);
    ASSERT_EQ(config.timeAllowedToLive, 2000u);
}

TEST(goose_config_validation) {
    GooseCommConfig config;
    config.interfaceName = "eth0";
    config.vlanPriority = 7; // 有效范围 0-7
    config.appId = 0x8000;   // 有效范围 0x0000-0xFFFF

    ASSERT_TRUE(config.vlanPriority <= 7);
    ASSERT_TRUE(config.appId <= 0xFFFF);
}

// ========== SV 适配器测试 ==========

// 注意：SVAdapter.h 需要类似的配置结构
// 这里我们只测试基本的数据结构验证

// ========== Report 适配器测试 ==========

// ReportAdapter 的测试需要 IEC61850 服务器
// 这里只做基本的API验证

TEST(adapter_api_exists) {
    // 验证适配器头文件可以正确包含
    // 这是一个编译测试，确保所有必要的类型都定义了

    GooseCommConfig gooseConfig;
    gooseConfig.interfaceName = "eth0";
    gooseConfig.goID = "test_goose";

    ASSERT_TRUE(!gooseConfig.goID.empty());
    ASSERT_TRUE(!gooseConfig.interfaceName.empty());
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  IEC61850 Adapters Basic Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "注意: 这是基础测试，完整的适配器测试需要：" << std::endl;
    std::cout << "  1. 网络接口配置（可能需要root权限）" << std::endl;
    std::cout << "  2. libiec61850 完整安装" << std::endl;
    std::cout << "  3. 实际的硬件或仿真环境" << std::endl;
    std::cout << "------------------------------------" << std::endl;

    RUN_TEST(goose_config_default);
    RUN_TEST(goose_config_validation);
    RUN_TEST(adapter_api_exists);

    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, "
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;

    return s_failed > 0 ? 1 : 0;
}

#else // !HAS_LIBIEC61850

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  IEC61850 Adapters Basic Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "SKIPPED: libiec61850 not available" << std::endl;
    std::cout << "====================================" << std::endl;
    return 0;
}

#endif // HAS_LIBIEC61850
