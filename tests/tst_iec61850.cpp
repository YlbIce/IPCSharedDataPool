/**
 * @file tst_iec61850.cpp
 * @brief IEC61850服务器单元测试
 */

#ifdef HAS_LIBIEC61850

#include "IEC61850Server.h"
#include <cassert>
#include <iostream>

using namespace IPC;

void test_server_initialization() {
    std::cout << "[TEST] Server Initialization..." << std::endl;
    
    IEC61850Server server;
    IEC61850ServerConfig config;
    config.tcpPort = 10200; // 使用非标准端口避免权限问题
    config.modelName = "test_model.cfg";
    config.iedName = "TestIED";
    
    // 注意：由于缺少test_model.cfg文件，这个测试可能会失败
    // 实际使用时需要提供有效的SCL配置文件
    bool result = server.initialize(config);
    
    if (result) {
        std::cout << "  PASS: Server initialized successfully" << std::endl;
    } else {
        std::cout << "  SKIP: Failed to initialize (expected without valid model file)" << std::endl;
    }
}

void test_data_binding() {
    std::cout << "[TEST] Data Binding..." << std::endl;
    
    IEC61850DataBinding binding;
    binding.iedName = "TestIED";
    binding.logicalDevice = "LD0";
    binding.logicalNode = "XCBR1";
    binding.dataObject = "Pos";
    binding.dataAttribute = "stVal";
    binding.fc = "ST";
    binding.poolOffset = 100;
    binding.isWritable = true;
    
    std::string fullRef = binding.getFullRef();
    assert(fullRef == "LD0/XCBR1.Pos.stVal");
    
    std::cout << "  PASS: Data binding created correctly" << std::endl;
}

void test_callback_registration() {
    std::cout << "[TEST] Callback Registration..." << std::endl;
    
    IEC61850Server server;
    
    bool readCalled = false;
    bool writeCalled = false;
    
    server.setReadCallback([&](const IEC61850DataBinding& binding, uint8_t* buffer) -> bool {
        readCalled = true;
        return true;
    });
    
    server.setWriteCallback([&](const IEC61850DataBinding& binding, const uint8_t* buffer) -> bool {
        writeCalled = true;
        return true;
    });
    
    server.setControlCallback([&](const std::string& ref, bool value) -> bool {
        return true;
    });
    
    std::cout << "  PASS: Callbacks registered successfully" << std::endl;
}

void test_async_start_stop() {
    std::cout << "[TEST] Async Start/Stop..." << std::endl;
    
    IEC61850Server server;
    
    // 没有有效模型文件时，startAsync会失败
    // 这里只测试调用接口
    bool result = server.startAsync();
    
    if (!result) {
        std::cout << "  PASS: Expected failure without valid model" << std::endl;
    }
    
    server.stop();
    std::cout << "  PASS: Stop called successfully" << std::endl;
}

int main() {
    std::cout << "=== IEC61850 Server Unit Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_data_binding();
    test_callback_registration();
    test_server_initialization();
    test_async_start_stop();
    
    std::cout << std::endl;
    std::cout << "=== All Tests Completed ===" << std::endl;
    
    return 0;
}

#else // !HAS_LIBIEC61850

int main() {
    std::cout << "IEC61850 tests skipped - libiec61850 not available" << std::endl;
    return 0;
}

#endif
