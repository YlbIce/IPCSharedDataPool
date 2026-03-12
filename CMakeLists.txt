cmake_minimum_required(VERSION 3.14)
project(IPCSharedDataPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 构建选项
option(BUILD_TESTS "构建单元测试" ON)
option(BUILD_APPS "构建应用程序" OFF)
option(BUILD_EXAMPLES "构建示例程序" OFF)
option(BUILD_IEC61850 "构建IEC61850功能模块" OFF)

# ========== libiec61850 集成 ==========

set(LIBIEC61850_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(LIBIEC61850_INCLUDE_DIR "${LIBIEC61850_ROOT}/include")
set(LIBIEC61850_LIBRARY_DIR "${LIBIEC61850_ROOT}/lib")

# 链接路径
link_directories(${LIBIEC61850_LIBRARY_DIR})
include_directories(${LIBIEC61850_INCLUDE_DIR})

if(BUILD_IEC61850)
    message(STATUS "Building IEC61850 module with libiec61850")
    add_definitions(-DHAS_LIBIEC61850)
endif()

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

set(CORE_SOURCES
    src/SharedDataPool.cpp
    src/IPCEventCenter.cpp
    src/DataPoolClient.cpp
    src/SOERecorder.cpp
    src/PersistentStorage.cpp
    src/VotingEngine.cpp
    src/IEC61850Mapper.cpp
)

if(BUILD_IEC61850)
    list(APPEND CORE_SOURCES
        src/IEC61850Server.cpp
        src/GooseAdapter.cpp
        src/GooseReceiverAdapter.cpp
        src/SVAdapter.cpp
src/SVAdapter.cpp
src/ReportAdapter.cpp
src/ReportAdapter.cpp
src/LogService.cpp
    )
endif()

add_library(IPCSharedDataPool SHARED ${CORE_SOURCES})

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

if(BUILD_IEC61850)
    target_link_libraries(IPCSharedDataPool
        PUBLIC
            iec61850
    )
endif()

# ========== 单元测试 ==========

if(BUILD_TESTS)
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

    # ProcessMonitor 测试
    add_executable(tst_process_monitor tests/tst_process_monitor.cpp)
    target_link_libraries(tst_process_monitor IPCSharedDataPool)
    add_test(NAME tst_process_monitor COMMAND tst_process_monitor)
    message(STATUS "  + tst_process_monitor")

    # Qt测试程序 (仅测试程序使用Qt)
    find_package(Qt5 COMPONENTS Core Test QUIET)
    if(Qt5_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
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
        set(CMAKE_AUTOMOC OFF)
        set(CMAKE_AUTORCC OFF)
        set(CMAKE_AUTOUIC OFF)
    else()
        message(STATUS "Qt5 not found, skipping Qt-based tests")
    endif()

    # 性能基准测试 (无Qt依赖)
    add_executable(tst_benchmark tests/tst_benchmark.cpp)
    target_link_libraries(tst_benchmark IPCSharedDataPool)
    
    # IEC61850测试
    if(BUILD_IEC61850)
        add_executable(tst_iec61850 tests/tst_iec61850.cpp)
        target_link_libraries(tst_iec61850 IPCSharedDataPool)
        add_test(NAME tst_iec61850 COMMAND tst_iec61850)
        message(STATUS "  + tst_iec61850")

        # LogService 测试
        add_executable(tst_log_service tests/tst_log_service.cpp)
        target_link_libraries(tst_log_service IPCSharedDataPool)
        add_test(NAME tst_log_service COMMAND tst_log_service)
        message(STATUS "  + tst_log_service")

        # 基础适配器测试
        add_executable(tst_adapters_basic tests/tst_adapters_basic.cpp)
        target_link_libraries(tst_adapters_basic IPCSharedDataPool)
        add_test(NAME tst_adapters_basic COMMAND tst_adapters_basic)
        message(STATUS "  + tst_adapters_basic")
    endif()
endif()

# ========== 安装配置 ==========

install(TARGETS IPCSharedDataPool pugixml
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(BUILD_IEC61850)
    install(FILES ${LIBIEC61850_LIBRARY_DIR}/libiec61850.so
        DESTINATION lib
    )
endif()

install(DIRECTORY include/ DESTINATION include)

# ========== 可视化测试APP ==========

if(BUILD_APPS)
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
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
        message(STATUS "Qt5 found: ${Qt5_VERSION}")
        message(STATUS "Building test apps with QCustomPlot...")
        
        # ========== QCustomPlot 图表库 ==========
        # QCustomPlot源码需要单独获取或使用系统安装版本
        find_path(QCUSTOMPLOT_INCLUDE_DIR qcustomplot.h
            PATHS /usr/include /usr/local/include
            PATH_SUFFIXES QCustomPlot
        )
        find_library(QCUSTOMPLOT_LIBRARY qcustomplot
            PATHS /usr/lib /usr/local/lib
        )
        
        if(QCUSTOMPLOT_INCLUDE_DIR AND QCUSTOMPLOT_LIBRARY)
            message(STATUS "Using system QCustomPlot")
        elseif(BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot/qcustomplot.cpp")
            add_library(qcustomplot STATIC
                examples/qcustomplot/qcustomplot.cpp
            )
            target_include_directories(qcustomplot PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot
            )
            target_link_libraries(qcustomplot
                Qt5::Core
                Qt5::Widgets
                Qt5::PrintSupport
            )
            set(QCUSTOMPLOT_LIBRARY qcustomplot)
            set(QCUSTOMPLOT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples/qcustomplot)
        else()
            message(WARNING "QCustomPlot not found, apps will be built without charts")
        endif()
        
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
            ${QCUSTOMPLOT_LIBRARY}
            Qt5::Core
            Qt5::Widgets
            Qt5::PrintSupport
        )
        if(QCUSTOMPLOT_INCLUDE_DIR)
            target_include_directories(ui_process PRIVATE ${QCUSTOMPLOT_INCLUDE_DIR})
        endif()
        
        # 业务进程 (带QCustomPlot趋势图)
        add_executable(business_process apps/business_process/main.cpp)
        target_link_libraries(business_process
            IPCSharedDataPool
            ${QCUSTOMPLOT_LIBRARY}
            Qt5::Core
            Qt5::Widgets
            Qt5::PrintSupport
        )
        if(QCUSTOMPLOT_INCLUDE_DIR)
            target_include_directories(business_process PRIVATE ${QCUSTOMPLOT_INCLUDE_DIR})
        endif()
        
        install(TARGETS comm_process ui_process business_process
            RUNTIME DESTINATION bin
        )
        
        message(STATUS "  + comm_process (通信进程)")
        message(STATUS "  + ui_process (UI进程)")
        message(STATUS "  + business_process (业务进程)")
    else()
        message(STATUS "Qt5 not found, skipping test apps")
    endif()
endif()
