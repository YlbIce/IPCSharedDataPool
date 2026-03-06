cmake_minimum_required(VERSION 3.14)
project(IPCSharedDataPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Qt自动moc (仅用于测试程序)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# ========== pugixml (轻量级XML解析库) ==========

add_library(pugixml STATIC
    pugixml/src/pugixml.cpp
)
target_include_directories(pugixml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/pugixml/src
)

# ========== 核心库 (纯C++，无Qt依赖) ==========

add_library(IPCSharedDataPool SHARED
    src/SharedDataPool.cpp
    src/IPCEventCenter.cpp
    src/DataPoolClient.cpp
    src/SOERecorder.cpp
    src/PersistentStorage.cpp
    src/VotingEngine.cpp
    src/IEC61850Mapper.cpp
)

target_include_directories(IPCSharedDataPool
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(IPCSharedDataPool
    PUBLIC
        pthread
        rt
        pugixml
)

# ========== 单元测试 ==========

enable_testing()

# 基础测试 (无Qt依赖)
add_executable(tst_common tests/tst_common.cpp)
target_link_libraries(tst_common IPCSharedDataPool)
add_test(NAME tst_common COMMAND tst_common)

add_executable(tst_rwlock tests/tst_rwlock.cpp)
target_link_libraries(tst_rwlock IPCSharedDataPool)
add_test(NAME tst_rwlock COMMAND tst_rwlock)

add_executable(tst_ringbuffer tests/tst_ringbuffer.cpp)
target_link_libraries(tst_ringbuffer IPCSharedDataPool)
add_test(NAME tst_ringbuffer COMMAND tst_ringbuffer)

add_executable(tst_shm_pool tests/tst_shm_pool.cpp)
target_link_libraries(tst_shm_pool IPCSharedDataPool)
add_test(NAME tst_shm_pool COMMAND tst_shm_pool)

add_executable(tst_event_center tests/tst_event_center.cpp)
target_link_libraries(tst_event_center IPCSharedDataPool)
add_test(NAME tst_event_center COMMAND tst_event_center)

add_executable(tst_integration tests/tst_integration.cpp)
target_link_libraries(tst_integration IPCSharedDataPool)
add_test(NAME tst_integration COMMAND tst_integration)

# Qt测试程序 (仅测试程序使用Qt)
find_package(Qt5 COMPONENTS Core Test REQUIRED)

add_executable(tst_soe_persist tests/tst_soe_persist.cpp)
target_link_libraries(tst_soe_persist 
    IPCSharedDataPool 
    Qt5::Core 
    Qt5::Test
)
add_test(NAME tst_soe_persist COMMAND tst_soe_persist)

add_executable(tst_voting_iec61850 tests/tst_voting_iec61850.cpp)
target_link_libraries(tst_voting_iec61850 
    IPCSharedDataPool 
    Qt5::Core 
    Qt5::Test
)
add_test(NAME tst_voting_iec61850 COMMAND tst_voting_iec61850)

# 性能基准测试 (无Qt依赖)
add_executable(tst_benchmark tests/tst_benchmark.cpp)
target_link_libraries(tst_benchmark IPCSharedDataPool)

# ========== 安装配置 ==========

install(TARGETS IPCSharedDataPool pugixml
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

# ========== 可视化测试APP ==========

# 设置Qt5路径 (如果需要)
if(DEFINED ENV{Qt5_DIR})
    set(CMAKE_PREFIX_PATH $ENV{Qt5_DIR} ${CMAKE_PREFIX_PATH})
elseif(EXISTS "/opt/Qt5.15.8/lib/cmake/Qt5")
    set(CMAKE_PREFIX_PATH "/opt/Qt5.15.8/lib/cmake/Qt5" ${CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_MODULE_PATH "/opt/Qt5.15.8/lib/cmake")
endif()

find_package(Qt5 COMPONENTS Core Widgets PrintSupport QUIET)
if(Qt5_FOUND)
    find_package(Qt5Charts QUIET)
endif()

if(Qt5_FOUND)
    message(STATUS "Qt5 found: ${Qt5_VERSION}")
    message(STATUS "Building test apps with QCustomPlot...")
    
    # ========== QCustomPlot 图表库 ==========
    add_library(qcustomplot STATIC
        examples/qcustomplot/qcustomplot.cpp
    )
    target_include_directories(qcustomplot PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot
    )
    target_link_libraries(qcustomplot
        Qt5::Core
        Qt5::Widgets
        Qt5::PrintSupport  # QCustomPlot需要打印支持
    )
    
    # 通信进程
    add_executable(comm_process apps/comm_process/main.cpp)
    target_link_libraries(comm_process
        IPCSharedDataPool
        Qt5::Core
        Qt5::Widgets
    )
    
    # UI进程 (带QCustomPlot趋势图)
    add_executable(ui_process apps/ui_process/main.cpp)
    target_link_libraries(ui_process
        IPCSharedDataPool
        qcustomplot
        Qt5::Core
        Qt5::Widgets
        Qt5::PrintSupport
    )
    target_include_directories(ui_process PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot
    )
    
    # 业务进程 (带QCustomPlot趋势图)
    add_executable(business_process apps/business_process/main.cpp)
    target_link_libraries(business_process
        IPCSharedDataPool
        qcustomplot
        Qt5::Core
        Qt5::Widgets
        Qt5::PrintSupport
    )
    target_include_directories(business_process PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot
    )
    
    install(TARGETS comm_process ui_process business_process
        RUNTIME DESTINATION bin
    )
    
    message(STATUS "  + comm_process (通信进程)")
    message(STATUS "  + ui_process (UI进程) - 含QCustomPlot趋势图")
    message(STATUS "  + business_process (业务进程) - 含QCustomPlot趋势图")
else()
    message(STATUS "Qt5 not found, skipping test apps")
endif()
