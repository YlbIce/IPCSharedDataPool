/**
 * @file business_process/main.cpp
 * @brief 业务进程 - 可视化数据处理与监控
 * 
 * 功能：
 * - 事件订阅与处理
 * - 三取二表决计算
 * - 告警监控与记录
 * - 点位趋势图（使用QCustomPlot）
 */

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QProgressBar>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QSplitter>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QVector>
#include <iostream>
#include <random>
#include <atomic>
#include <deque>
#include "../../include/DataPoolClient.h"
#include "../../include/ProcessMonitor.h"
#include "../../examples/qcustomplot/qcustomplot.h"

using namespace IPC;

/**
 * @brief 业务进程主窗口
 */
class BusinessProcessWindow : public QMainWindow {
    Q_OBJECT

public:
    BusinessProcessWindow(QWidget* parent = nullptr) 
        : QMainWindow(parent), m_client(nullptr), m_processMonitor(nullptr), m_subId(INVALID_INDEX),
          m_eventCount(0), m_alarmCount(0) {
        m_processMonitor = new ProcessMonitor();
        setupUI();
        setupMenu();
    }
    
    ~BusinessProcessWindow() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
        if (m_processMonitor) {
            delete m_processMonitor;
        }
    }

private:
    void setupUI() {
        setWindowTitle("业务进程 - 数据处理与监控");
        resize(1300, 850);
        
        QWidget* central = new QWidget(this);
        setCentralWidget(central);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        
        // 顶部连接区
        QGroupBox* connGroup = new QGroupBox("连接配置", this);
        QHBoxLayout* connLayout = new QHBoxLayout(connGroup);
        
        connLayout->addWidget(new QLabel("数据池名称:"));
        m_poolNameEdit = new QLineEdit("/ipc_data_pool", this);
        connLayout->addWidget(m_poolNameEdit);
        
        m_connectBtn = new QPushButton("连接", this);
        m_connectBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
        connLayout->addWidget(m_connectBtn);
        
        m_disconnectBtn = new QPushButton("断开", this);
        m_disconnectBtn->setEnabled(false);
        m_disconnectBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
        connLayout->addWidget(m_disconnectBtn);
        
        connLayout->addStretch();
        
        m_statusLabel = new QLabel("未连接", this);
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        connLayout->addWidget(m_statusLabel);
        
        mainLayout->addWidget(connGroup);
        
        // Tab页面
        QTabWidget* tabWidget = new QTabWidget(this);
        
        // ====== 事件处理页 ======
        QWidget* eventTab = new QWidget();
        QVBoxLayout* eventLayout = new QVBoxLayout(eventTab);
        
        // 统计面板
        QGridLayout* statsGrid = new QGridLayout();
        statsGrid->addWidget(new QLabel("事件处理数:"), 0, 0);
        m_eventCountLabel = new QLabel("0");
        m_eventCountLabel->setStyleSheet("font-weight: bold; color: #F44336; font-size: 14px;");
        statsGrid->addWidget(m_eventCountLabel, 0, 1);
        
        statsGrid->addWidget(new QLabel("YX变位:"), 0, 2);
        m_yxChangeEventLabel = new QLabel("0");
        m_yxChangeEventLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
        statsGrid->addWidget(m_yxChangeEventLabel, 0, 3);
        
        statsGrid->addWidget(new QLabel("YC更新:"), 0, 4);
        m_ycUpdateEventLabel = new QLabel("0");
        m_ycUpdateEventLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        statsGrid->addWidget(m_ycUpdateEventLabel, 0, 5);
        
        statsGrid->addWidget(new QLabel("待处理事件:"), 0, 6);
        m_pendingEventLabel = new QLabel("0");
        statsGrid->addWidget(m_pendingEventLabel, 0, 7);
        
        // 系统监控信息（第二行）
        statsGrid->addWidget(new QLabel("进程CPU:"), 1, 0);
        m_procCpuLabel = new QLabel("0.0%");
        m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
        statsGrid->addWidget(m_procCpuLabel, 1, 1);
        
        statsGrid->addWidget(new QLabel("进程内存:"), 1, 2);
        m_procMemLabel = new QLabel("0 MB");
        m_procMemLabel->setStyleSheet("font-weight: bold; color: #673AB7;");
        statsGrid->addWidget(m_procMemLabel, 1, 3);
        
        statsGrid->addWidget(new QLabel("系统CPU:"), 1, 4);
        m_sysCpuLabel = new QLabel("0.0%");
        m_sysCpuLabel->setStyleSheet("font-weight: bold; color: #00BCD4;");
        statsGrid->addWidget(m_sysCpuLabel, 1, 5);
        
        statsGrid->addWidget(new QLabel("系统内存:"), 1, 6);
        m_sysMemLabel = new QLabel("0.0%");
        m_sysMemLabel->setStyleSheet("font-weight: bold; color: #8BC34A;");
        statsGrid->addWidget(m_sysMemLabel, 1, 7);
        
        statsGrid->setColumnStretch(8, 1);
        eventLayout->addLayout(statsGrid);
        
        // 事件列表
        QHBoxLayout* eventCtrlLayout = new QHBoxLayout();
        eventCtrlLayout->addWidget(new QLabel("订阅状态:"));
        m_subStatusLabel = new QLabel("未订阅");
        eventCtrlLayout->addWidget(m_subStatusLabel);
        
        m_subscribeBtn = new QPushButton("订阅事件", this);
        connect(m_subscribeBtn, &QPushButton::clicked, this, &BusinessProcessWindow::subscribeEvents);
        eventCtrlLayout->addWidget(m_subscribeBtn);
        
        m_unsubscribeBtn = new QPushButton("取消订阅", this);
        m_unsubscribeBtn->setEnabled(false);
        connect(m_unsubscribeBtn, &QPushButton::clicked, this, &BusinessProcessWindow::unsubscribeEvents);
        eventCtrlLayout->addWidget(m_unsubscribeBtn);
        
        eventCtrlLayout->addStretch();
        eventLayout->addLayout(eventCtrlLayout);
        
        m_eventTable = new QTableWidget(this);
        m_eventTable->setColumnCount(7);
        m_eventTable->setHorizontalHeaderLabels({"时间", "Key", "类型", "旧值", "新值", "质量", "来源"});
        m_eventTable->horizontalHeader()->setStretchLastSection(true);
        eventLayout->addWidget(m_eventTable);
        
        tabWidget->addTab(eventTab, "事件处理");
        
        // ====== 表决计算页 ======
        QWidget* votingTab = new QWidget();
        QVBoxLayout* votingLayout = new QVBoxLayout(votingTab);
        
        QHBoxLayout* votingCtrlLayout = new QHBoxLayout();
        QPushButton* addGroupBtn = new QPushButton("添加表决组", this);
        connect(addGroupBtn, &QPushButton::clicked, this, &BusinessProcessWindow::addVotingGroup);
        votingCtrlLayout->addWidget(addGroupBtn);
        
        QPushButton* execAllBtn = new QPushButton("执行全部表决", this);
        execAllBtn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold;");
        connect(execAllBtn, &QPushButton::clicked, this, &BusinessProcessWindow::executeAllVoting);
        votingCtrlLayout->addWidget(execAllBtn);
        
        votingCtrlLayout->addWidget(new QLabel("自动表决间隔(ms):"));
        m_autoVotingSpin = new QSpinBox(this);
        m_autoVotingSpin->setRange(10, 10000);
        m_autoVotingSpin->setValue(1000);
        votingCtrlLayout->addWidget(m_autoVotingSpin);
        
        m_autoVotingCheck = new QCheckBox("启用自动表决", this);
        votingCtrlLayout->addWidget(m_autoVotingCheck);
        
        votingCtrlLayout->addStretch();
        votingLayout->addLayout(votingCtrlLayout);
        
        // 表决组配置表
        QGroupBox* votingGroupGroup = new QGroupBox("表决组配置与结果", this);
        QVBoxLayout* votingGroupLayout = new QVBoxLayout(votingGroupGroup);
        m_votingTable = new QTableWidget(this);
        m_votingTable->setColumnCount(10);
        m_votingTable->setHorizontalHeaderLabels({
            "组ID", "名称", "源A", "源B", "源C", 
            "策略", "结果", "值", "有效源数", "告警"
        });
        m_votingTable->horizontalHeader()->setStretchLastSection(true);
        votingGroupLayout->addWidget(m_votingTable);
        votingLayout->addWidget(votingGroupGroup);
        
        // 表决统计
        QGroupBox* votingStatsGroup = new QGroupBox("表决统计", this);
        QGridLayout* votingStatsGrid = new QGridLayout(votingStatsGroup);
        
        votingStatsGrid->addWidget(new QLabel("总表决次数:"), 0, 0);
        m_totalVotingLabel = new QLabel("0");
        votingStatsGrid->addWidget(m_totalVotingLabel, 0, 1);
        
        votingStatsGrid->addWidget(new QLabel("一致次数:"), 0, 2);
        m_unanimousLabel = new QLabel("0");
        votingStatsGrid->addWidget(m_unanimousLabel, 0, 3);
        
        votingStatsGrid->addWidget(new QLabel("多数次数:"), 0, 4);
        m_majorityLabel = new QLabel("0");
        votingStatsGrid->addWidget(m_majorityLabel, 0, 5);
        
        votingStatsGrid->addWidget(new QLabel("不一致次数:"), 0, 6);
        m_disagreeLabel = new QLabel("0");
        votingStatsGrid->addWidget(m_disagreeLabel, 0, 7);
        
        votingStatsGrid->setColumnStretch(8, 1);
        votingLayout->addWidget(votingStatsGroup);
        
        tabWidget->addTab(votingTab, "表决计算");
        
        // ====== 告警监控页 ======
        QWidget* alarmTab = new QWidget();
        QVBoxLayout* alarmLayout = new QVBoxLayout(alarmTab);
        
        QHBoxLayout* alarmCtrlLayout = new QHBoxLayout();
        QPushButton* clearAlarmBtn = new QPushButton("清除所有告警", this);
        connect(clearAlarmBtn, &QPushButton::clicked, this, [this]() {
            m_alarmTable->setRowCount(0);
            m_alarmCount = 0;
            m_alarmCountLabel->setText("0");
            appendLog("所有告警已清除");
        });
        alarmCtrlLayout->addWidget(clearAlarmBtn);
        alarmCtrlLayout->addStretch();
        alarmLayout->addLayout(alarmCtrlLayout);
        
        QGroupBox* alarmStatsGroup = new QGroupBox("告警统计", this);
        QHBoxLayout* alarmStatsLayout = new QHBoxLayout(alarmStatsGroup);
        alarmStatsLayout->addWidget(new QLabel("活跃告警数:"));
        m_alarmCountLabel = new QLabel("0");
        m_alarmCountLabel->setStyleSheet("font-weight: bold; color: #F44336;");
        alarmStatsLayout->addWidget(m_alarmCountLabel);
        alarmStatsLayout->addStretch();
        alarmLayout->addWidget(alarmStatsGroup);
        
        m_alarmTable = new QTableWidget(this);
        m_alarmTable->setColumnCount(6);
        m_alarmTable->setHorizontalHeaderLabels({"时间", "类型", "来源", "级别", "消息", "状态"});
        m_alarmTable->horizontalHeader()->setStretchLastSection(true);
        alarmLayout->addWidget(m_alarmTable);
        
        tabWidget->addTab(alarmTab, "告警监控");
        
        // ====== 数据监控页（带QCustomPlot趋势图） ======
        QWidget* monitorTab = new QWidget();
        QVBoxLayout* monitorLayout = new QVBoxLayout(monitorTab);
        
        QHBoxLayout* monitorCtrlLayout = new QHBoxLayout();
        monitorCtrlLayout->addWidget(new QLabel("监控点Key:"));
        m_monitorKeyEdit = new QLineEdit("2:0", this);
        m_monitorKeyEdit->setPlaceholderText("设备地址:点号 (默认YC点)");
        monitorCtrlLayout->addWidget(m_monitorKeyEdit);
        
        monitorCtrlLayout->addWidget(new QLabel("采样间隔(ms):"));
        m_sampleIntervalSpin = new QSpinBox(this);
        m_sampleIntervalSpin->setRange(10, 5000);
        m_sampleIntervalSpin->setValue(100);
        monitorCtrlLayout->addWidget(m_sampleIntervalSpin);
        
        monitorCtrlLayout->addWidget(new QLabel("显示点数:"));
        m_displayPointsSpin = new QSpinBox(this);
        m_displayPointsSpin->setRange(50, 2000);
        m_displayPointsSpin->setValue(500);
        monitorCtrlLayout->addWidget(m_displayPointsSpin);
        
        QPushButton* startMonitorBtn = new QPushButton("开始监控", this);
        startMonitorBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
        connect(startMonitorBtn, &QPushButton::clicked, this, &BusinessProcessWindow::startMonitoring);
        monitorCtrlLayout->addWidget(startMonitorBtn);
        
        QPushButton* stopMonitorBtn = new QPushButton("停止监控", this);
        stopMonitorBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
        connect(stopMonitorBtn, &QPushButton::clicked, this, &BusinessProcessWindow::stopMonitoring);
        monitorCtrlLayout->addWidget(stopMonitorBtn);
        
        QPushButton* exportPngBtn = new QPushButton("导出PNG", this);
        connect(exportPngBtn, &QPushButton::clicked, this, &BusinessProcessWindow::exportTrendPNG);
        monitorCtrlLayout->addWidget(exportPngBtn);
        
        monitorCtrlLayout->addStretch();
        monitorLayout->addLayout(monitorCtrlLayout);
        
        // QCustomPlot趋势图
        m_plot = new QCustomPlot(this);
        setupPlot();
        monitorLayout->addWidget(m_plot, 1);  // 拉伸因子为1
        
        // 统计信息
        QHBoxLayout* statsLayout = new QHBoxLayout();
        m_currentValueLabel = new QLabel("当前值: --");
        m_currentValueLabel->setStyleSheet("font-weight: bold; color: #2196F3; font-size: 14px;");
        statsLayout->addWidget(m_currentValueLabel);
        
        m_minValueLabel = new QLabel("最小: --");
        statsLayout->addWidget(m_minValueLabel);
        
        m_maxValueLabel = new QLabel("最大: --");
        statsLayout->addWidget(m_maxValueLabel);
        
        m_avgValueLabel = new QLabel("平均: --");
        statsLayout->addWidget(m_avgValueLabel);
        
        statsLayout->addStretch();
        monitorLayout->addLayout(statsLayout);
        
        tabWidget->addTab(monitorTab, "数据监控");
        
        mainLayout->addWidget(tabWidget);
        
        // 底部日志
        QGroupBox* logGroup = new QGroupBox("运行日志", this);
        QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
        m_logEdit = new QTextEdit(this);
        m_logEdit->setReadOnly(true);
        m_logEdit->setMaximumHeight(120);
        m_logEdit->setStyleSheet("font-family: Consolas; font-size: 10px;");
        logLayout->addWidget(m_logEdit);
        mainLayout->addWidget(logGroup);
        
        // 连接信号
        connect(m_connectBtn, &QPushButton::clicked, this, &BusinessProcessWindow::connectToPool);
        connect(m_disconnectBtn, &QPushButton::clicked, this, &BusinessProcessWindow::disconnectFromPool);
        connect(m_autoVotingCheck, &QCheckBox::toggled, this, &BusinessProcessWindow::toggleAutoVoting);
        
        // 定时器
        m_refreshTimer = new QTimer(this);
        connect(m_refreshTimer, &QTimer::timeout, this, &BusinessProcessWindow::refresh);
        
        m_eventProcessTimer = new QTimer(this);
        connect(m_eventProcessTimer, &QTimer::timeout, this, &BusinessProcessWindow::processEvents);
        
        m_votingTimer = new QTimer(this);
        connect(m_votingTimer, &QTimer::timeout, this, &BusinessProcessWindow::executeAllVoting);
        
        m_monitorTimer = new QTimer(this);
        connect(m_monitorTimer, &QTimer::timeout, this, &BusinessProcessWindow::updateMonitorData);
    }
    
    void setupPlot() {
        // 添加曲线
        m_plot->addGraph();
        m_plot->graph(0)->setPen(QPen(QColor(33, 150, 243), 2));  // 蓝色线条
        m_plot->graph(0)->setBrush(QBrush(QColor(33, 150, 243, 30)));  // 半透明填充
        
        // 设置坐标轴
        m_plot->xAxis->setLabel("时间 (秒)");
        m_plot->yAxis->setLabel("YC值");
        m_plot->xAxis->setRange(0, 60);
        m_plot->yAxis->setRange(-10, 110);
        
        // 网格
        m_plot->xAxis->grid()->setVisible(true);
        m_plot->yAxis->grid()->setVisible(true);
        m_plot->xAxis->grid()->setPen(QPen(QColor(200, 200, 200), 1, Qt::DotLine));
        m_plot->yAxis->grid()->setPen(QPen(QColor(200, 200, 200), 1, Qt::DotLine));
        
        // 交互：拖拽、缩放
        m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
        
        // 图例
        m_plot->legend->setVisible(true);
        m_plot->graph(0)->setName("YC监控曲线");
        m_plot->legend->setBrush(QColor(255, 255, 255, 150));
        
        // 时间轴格式化
        QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
        timeTicker->setTimeFormat("%m:%s");
        m_plot->xAxis->setTicker(timeTicker);
        
        m_plot->axisRect()->setupFullAxesBox();
        
        // 同步上下和左右坐标轴
        connect(m_plot->xAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->xAxis2, SLOT(setRange(QCPRange)));
        connect(m_plot->yAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->yAxis2, SLOT(setRange(QCPRange)));
    }
    
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
        
        QAction* exitAction = new QAction("退出(&X)", this);
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
        fileMenu->addAction(exitAction);
        
        QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
        QAction* aboutAction = new QAction("关于(&A)", this);
        connect(aboutAction, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, "关于", 
                "业务进程 - 数据处理与监控\n\n"
                "事件处理、表决计算、告警监控\n"
                "趋势图使用QCustomPlot\n"
                "版本: 1.1.0");
        });
        helpMenu->addAction(aboutAction);
    }

private slots:
    void connectToPool() {
        if (m_client) return;
        
        DataPoolClient::Config config;
        config.poolName = m_poolNameEdit->text().toStdString();
        config.eventName = "/ipc_events";
        config.soeName = "/ipc_soe";
        config.processName = "business_process";
        config.create = false;
        config.enableVoting = true;
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            appendLog("错误: 连接数据池失败!");
            QMessageBox::critical(this, "错误", "连接数据池失败!");
            return;
        }
        
        m_client->startHeartbeat(1000);
        
        // 设置表决告警回调
        m_client->setVotingAlarmCallback([this](uint32_t groupId, DeviationLevel level, const char* msg) {
            QMetaObject::invokeMethod(this, [this, groupId, level, msg]() {
                addAlarm("表决偏差", QString("组%1: %2").arg(groupId).arg(msg), level);
            });
        });
        
        m_connectBtn->setEnabled(false);
        m_disconnectBtn->setEnabled(true);
        m_poolNameEdit->setEnabled(false);
        
        m_statusLabel->setText("已连接");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        m_refreshTimer->start(500);
        m_eventProcessTimer->start(50);
        
        appendLog("已连接到数据池");
    }
    
    void disconnectFromPool() {
        stopMonitoring();
        m_votingTimer->stop();
        m_refreshTimer->stop();
        m_eventProcessTimer->stop();
        
        if (m_subId != INVALID_INDEX) {
            m_client->unsubscribe(m_subId);
            m_subId = INVALID_INDEX;
        }
        
        if (m_client) {
            m_client->shutdown();
            delete m_client;
            m_client = nullptr;
        }
        
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        m_poolNameEdit->setEnabled(true);
        m_subscribeBtn->setEnabled(true);
        m_unsubscribeBtn->setEnabled(false);
        
        m_statusLabel->setText("未连接");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_subStatusLabel->setText("未订阅");
        
        appendLog("已断开连接");
    }
    
    void subscribeEvents() {
        if (!m_client || m_subId != INVALID_INDEX) return;
        
        m_subId = m_client->subscribe([this](const Event& event) {
            QMetaObject::invokeMethod(this, [this, event]() {
                handleEvent(event);
            });
        });
        
        if (m_subId != INVALID_INDEX) {
            m_subscribeBtn->setEnabled(false);
            m_unsubscribeBtn->setEnabled(true);
            m_subStatusLabel->setText(QString("已订阅 (ID:%1)").arg(m_subId));
            appendLog(QString("事件订阅成功, ID: %1").arg(m_subId));
        }
    }
    
    void unsubscribeEvents() {
        if (!m_client || m_subId == INVALID_INDEX) return;
        
        m_client->unsubscribe(m_subId);
        m_subId = INVALID_INDEX;
        
        m_subscribeBtn->setEnabled(true);
        m_unsubscribeBtn->setEnabled(false);
        m_subStatusLabel->setText("未订阅");
        appendLog("已取消事件订阅");
    }
    
    void handleEvent(const Event& event) {
        m_eventCount++;
        
        QString typeStr;
        switch (event.pointType) {
            case PointType::YX: 
                typeStr = "YX"; 
                m_yxChangeEvents++;
                break;
            case PointType::YC: 
                typeStr = "YC"; 
                m_ycUpdateEvents++;
                break;
            case PointType::DZ: typeStr = "DZ"; break;
            case PointType::YK: typeStr = "YK"; break;
        }
        
        // 添加到事件表（保留最近100条）
        if (m_eventTable->rowCount() >= 100) {
            m_eventTable->removeRow(0);
        }
        
        int row = m_eventTable->rowCount();
        m_eventTable->insertRow(row);
        
        m_eventTable->setItem(row, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(event.timestamp).toString("hh:mm:ss")));
        m_eventTable->setItem(row, 1, new QTableWidgetItem(
            QString("%1:%2").arg(event.addr).arg(event.id)));
        m_eventTable->setItem(row, 2, new QTableWidgetItem(typeStr));
        m_eventTable->setItem(row, 3, new QTableWidgetItem(
            event.pointType == PointType::YC ? 
                QString::number(event.oldValue.floatValue, 'f', 2) :
                QString::number(event.oldValue.intValue)));
        m_eventTable->setItem(row, 4, new QTableWidgetItem(
            event.pointType == PointType::YC ? 
                QString::number(event.newValue.floatValue, 'f', 2) :
                QString::number(event.newValue.intValue)));
        m_eventTable->setItem(row, 5, new QTableWidgetItem(QString::number(event.quality)));
        m_eventTable->setItem(row, 6, new QTableWidgetItem(
            QString("%1 (%2)").arg(QString::fromUtf8(event.source)).arg(event.sourcePid)));
        
        // 关键事件告警
        if (event.isCritical) {
            addAlarm("关键事件", QString("%1 %2:%3 变化")
                .arg(typeStr).arg(event.addr).arg(event.id), DeviationLevel::MODERATE);
        }
    }
    
    void processEvents() {
        if (m_client && m_subId != INVALID_INDEX) {
            m_client->processEvents(m_subId, 100);
        }
    }
    
    void addVotingGroup() {
        if (!m_client) return;
        
        VotingConfig config;
        config.groupId = m_votingTable->rowCount();
        snprintf(config.name, sizeof(config.name), "表决组%d", config.groupId);
        
        // 使用前几个YX点作为示例
        config.sourceKeyA = makeKey(1, 0);
        config.sourceKeyB = makeKey(1, 1);
        config.sourceKeyC = makeKey(1, 2);
        config.sourceType = 0;  // YX
        config.votingStrategy = 0;  // 严格三取二
        config.enableDeviation = 1;
        
        uint32_t groupId = m_client->addVotingGroup(config);
        if (groupId != INVALID_INDEX) {
            appendLog(QString("添加表决组: %1").arg(groupId));
            refreshVotingTable();
        }
    }
    
    void executeAllVoting() {
        if (!m_client) return;
        
        VotingEngine* engine = m_client->getVotingEngine();
        if (!engine) return;
        
        uint32_t count = engine->getVotingGroupCount();
        
        m_totalVoting++;
        
        for (uint32_t i = 0; i < count; i++) {
            VotingOutput output;
            if (m_client->performVotingYX(i, output)) {
                QString resultStr;
                switch (static_cast<VotingResult>(output.result)) {
                    case VotingResult::UNANIMOUS: 
                        resultStr = "一致"; 
                        m_unanimous++;
                        break;
                    case VotingResult::MAJORITY: 
                        resultStr = "多数"; 
                        m_majority++;
                        break;
                    case VotingResult::DISAGREE: 
                        resultStr = "不一致"; 
                        m_disagree++;
                        break;
                    default: 
                        resultStr = "失败";
                }
                
                m_votingTable->setItem(i, 6, new QTableWidgetItem(resultStr));
                m_votingTable->setItem(i, 7, new QTableWidgetItem(QString::number(output.yxValue)));
                m_votingTable->setItem(i, 8, new QTableWidgetItem(QString::number(output.validSourceCount)));
                m_votingTable->setItem(i, 9, new QTableWidgetItem(QString::number(output.alarmFlags)));
                
                // 偏差告警
                if (output.deviationLevel > 0) {
                    addAlarm("表决偏差", QString("组%1 偏差级别:%2")
                        .arg(i).arg(output.deviationLevel), 
                        static_cast<DeviationLevel>(output.deviationLevel));
                }
            }
        }
    }
    
    void toggleAutoVoting(bool enabled) {
        if (enabled) {
            m_votingTimer->start(m_autoVotingSpin->value());
            appendLog("自动表决已启用");
        } else {
            m_votingTimer->stop();
            appendLog("自动表决已停止");
        }
    }
    
    void addAlarm(const QString& type, const QString& source, DeviationLevel level) {
        m_alarmCount++;
        
        if (m_alarmTable->rowCount() >= 200) {
            m_alarmTable->removeRow(0);
        }
        
        int row = m_alarmTable->rowCount();
        m_alarmTable->insertRow(row);
        
        m_alarmTable->setItem(row, 0, new QTableWidgetItem(
            QDateTime::currentDateTime().toString("hh:mm:ss")));
        m_alarmTable->setItem(row, 1, new QTableWidgetItem(type));
        m_alarmTable->setItem(row, 2, new QTableWidgetItem(source));
        
        QString levelStr;
        QColor levelColor;
        switch (level) {
            case DeviationLevel::MINOR:
                levelStr = "轻微";
                levelColor = QColor("#FFC107");
                break;
            case DeviationLevel::MODERATE:
                levelStr = "中等";
                levelColor = QColor("#FF9800");
                break;
            case DeviationLevel::SEVERE:
                levelStr = "严重";
                levelColor = QColor("#F44336");
                break;
            default:
                levelStr = "信息";
                levelColor = QColor("#2196F3");
        }
        
        QTableWidgetItem* levelItem = new QTableWidgetItem(levelStr);
        levelItem->setBackground(QBrush(levelColor));
        levelItem->setForeground(QBrush(Qt::white));
        m_alarmTable->setItem(row, 3, levelItem);
        
        m_alarmTable->setItem(row, 4, new QTableWidgetItem(source));
        m_alarmTable->setItem(row, 5, new QTableWidgetItem("活跃"));
        
        m_alarmCountLabel->setText(QString::number(m_alarmCount.load()));
    }
    
    void startMonitoring() {
        if (!m_client) return;
        
        QString keyText = m_monitorKeyEdit->text();
        auto parts = keyText.split(':');
        if (parts.size() != 2) {
            QMessageBox::warning(this, "错误", "请输入正确的Key格式 (设备地址:点号)");
            return;
        }
        
        m_monitorKey = makeKey(parts[0].toInt(), parts[1].toInt());
        m_startTime = QDateTime::currentDateTime();
        
        // 清除旧数据
        m_plot->graph(0)->data()->clear();
        m_plot->xAxis->setRange(0, 60);
        m_plot->replot();
        
        m_monitorTimer->start(m_sampleIntervalSpin->value());
        appendLog(QString("开始监控点: %1").arg(keyText));
    }
    
    void stopMonitoring() {
        m_monitorTimer->stop();
        appendLog("停止监控");
    }
    
    void updateMonitorData() {
        if (!m_client) return;
        
        float value;
        uint8_t quality;
        if (!m_client->getYC(m_monitorKey, value, quality)) return;
        
        // 计算时间（秒）
        double time = m_startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;
        
        // 添加数据点
        m_plot->graph(0)->addData(time, value);
        
        // 限制数据点数量
        int maxPoints = m_displayPointsSpin->value();
        while (m_plot->graph(0)->data()->size() > maxPoints) {
            m_plot->graph(0)->data()->remove(m_plot->graph(0)->data()->begin()->key);
        }
        
        // 自动滚动X轴
        m_plot->xAxis->setRange(time, 60, Qt::AlignRight);
        
        // 自动调整Y轴范围
        m_plot->graph(0)->rescaleValueAxis(false, true);
        
        // 重绘
        m_plot->replot();
        
        // 更新统计
        m_currentValueLabel->setText(QString("当前值: %1").arg(value, 0, 'f', 2));
        
        if (!m_plot->graph(0)->data()->isEmpty()) {
            double minVal = 1e30, maxVal = -1e30, sum = 0;
            int count = 0;
            for (auto it = m_plot->graph(0)->data()->begin(); it != m_plot->graph(0)->data()->end(); ++it) {
                double v = it->value;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
                sum += v;
                count++;
            }
            double avg = count > 0 ? sum / count : 0;
            
            m_minValueLabel->setText(QString("最小: %1").arg(minVal, 0, 'f', 2));
            m_maxValueLabel->setText(QString("最大: %1").arg(maxVal, 0, 'f', 2));
            m_avgValueLabel->setText(QString("平均: %1").arg(avg, 0, 'f', 2));
        }
    }
    
    void exportTrendPNG() {
        QString filename = QString("trend_%1_%2.png")
            .arg(m_monitorKey)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        if (m_plot->savePng(filename, 1200, 800)) {
            QMessageBox::information(this, "导出成功", QString("趋势图已保存到: %1").arg(filename));
            appendLog(QString("导出趋势图: %1").arg(filename));
        }
    }
    
    void refresh() {
        if (!m_client) return;
        
        // 更新统计
        m_eventCountLabel->setText(QString::number(m_eventCount.load()));
        m_yxChangeEventLabel->setText(QString::number(m_yxChangeEvents.load()));
        m_ycUpdateEventLabel->setText(QString::number(m_ycUpdateEvents.load()));
        
        if (m_subId != INVALID_INDEX) {
            uint32_t pending = m_client->getEventCenter()->getPendingEvents(m_subId);
            m_pendingEventLabel->setText(QString::number(pending));
        }
        
        // 更新表决统计
        m_totalVotingLabel->setText(QString::number(m_totalVoting.load()));
        m_unanimousLabel->setText(QString::number(m_unanimous.load()));
        m_majorityLabel->setText(QString::number(m_majority.load()));
        m_disagreeLabel->setText(QString::number(m_disagree.load()));
        
        // 更新系统监控信息
        updateSystemMonitor();
        
        refreshVotingTable();
    }
    
    void updateSystemMonitor() {
        if (!m_processMonitor) return;
        
        // 获取进程资源信息
        ProcessResourceInfo procInfo;
        if (m_processMonitor->getProcessInfo(procInfo)) {
            m_procCpuLabel->setText(QString("%1%").arg(procInfo.cpuPercent, 0, 'f', 1));
            m_procMemLabel->setText(QString::fromStdString(
                ProcessMonitor::formatMemory(procInfo.vmRSS)));
            
            // CPU使用率高时改变颜色
            if (procInfo.cpuPercent > 80) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #F44336;");
            } else if (procInfo.cpuPercent > 50) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
            } else {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
            }
        }
        
        // 获取系统资源信息
        SystemResourceInfo sysInfo;
        if (m_processMonitor->getSystemInfo(sysInfo)) {
            m_sysCpuLabel->setText(QString("%1%").arg(sysInfo.cpuUsage, 0, 'f', 1));
            m_sysMemLabel->setText(QString("%1%").arg(sysInfo.memoryUsage, 0, 'f', 1));
        }
    }
    
    void refreshVotingTable() {
        if (!m_client) return;
        
        VotingEngine* engine = m_client->getVotingEngine();
        if (!engine) return;
        
        uint32_t count = engine->getVotingGroupCount();
        m_votingTable->setRowCount(count);
        
        for (uint32_t i = 0; i < count; i++) {
            VotingConfig config;
            if (engine->getVotingGroupConfig(i, config)) {
                m_votingTable->setItem(i, 0, new QTableWidgetItem(QString::number(config.groupId)));
                m_votingTable->setItem(i, 1, new QTableWidgetItem(config.name));
                m_votingTable->setItem(i, 2, new QTableWidgetItem(QString::number(config.sourceKeyA)));
                m_votingTable->setItem(i, 3, new QTableWidgetItem(QString::number(config.sourceKeyB)));
                m_votingTable->setItem(i, 4, new QTableWidgetItem(QString::number(config.sourceKeyC)));
                
                QString strategyStr;
                switch (config.votingStrategy) {
                    case 0: strategyStr = "严格三取二"; break;
                    case 1: strategyStr = "宽松"; break;
                    case 2: strategyStr = "优先级"; break;
                    default: strategyStr = "未知";
                }
                m_votingTable->setItem(i, 5, new QTableWidgetItem(strategyStr));
            }
        }
    }
    
    void appendLog(const QString& msg) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_logEdit->append(QString("[%1] %2").arg(timestamp, msg));
    }

private:
    DataPoolClient* m_client;
    ProcessMonitor* m_processMonitor;
    uint32_t m_subId;
    
    std::atomic<uint64_t> m_eventCount{0};
    std::atomic<uint64_t> m_yxChangeEvents{0};
    std::atomic<uint64_t> m_ycUpdateEvents{0};
    std::atomic<uint64_t> m_alarmCount{0};
    std::atomic<uint64_t> m_totalVoting{0};
    std::atomic<uint64_t> m_unanimous{0};
    std::atomic<uint64_t> m_majority{0};
    std::atomic<uint64_t> m_disagree{0};
    
    uint64_t m_monitorKey{0};
    QDateTime m_startTime;
    
    // UI组件
    QLineEdit* m_poolNameEdit;
    QPushButton* m_connectBtn;
    QPushButton* m_disconnectBtn;
    QLabel* m_statusLabel;
    
    QLabel* m_eventCountLabel;
    QLabel* m_yxChangeEventLabel;
    QLabel* m_ycUpdateEventLabel;
    QLabel* m_pendingEventLabel;
    QLabel* m_alarmCountLabel;
    QLabel* m_subStatusLabel;
    
    QLabel* m_totalVotingLabel;
    QLabel* m_unanimousLabel;
    QLabel* m_majorityLabel;
    QLabel* m_disagreeLabel;
    
    QLabel* m_currentValueLabel;
    QLabel* m_minValueLabel;
    QLabel* m_maxValueLabel;
    QLabel* m_avgValueLabel;
    
    // 系统监控标签
    QLabel* m_procCpuLabel;
    QLabel* m_procMemLabel;
    QLabel* m_sysCpuLabel;
    QLabel* m_sysMemLabel;
    
    QPushButton* m_subscribeBtn;
    QPushButton* m_unsubscribeBtn;
    QCheckBox* m_autoVotingCheck;
    QSpinBox* m_autoVotingSpin;
    
    QLineEdit* m_monitorKeyEdit;
    QSpinBox* m_sampleIntervalSpin;
    QSpinBox* m_displayPointsSpin;
    QTableWidget* m_eventTable;
    QTableWidget* m_votingTable;
    QTableWidget* m_alarmTable;
    QTextEdit* m_logEdit;
    
    QCustomPlot* m_plot;
    QTimer* m_refreshTimer;
    QTimer* m_eventProcessTimer;
    QTimer* m_votingTimer;
    QTimer* m_monitorTimer;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("business_process");
    QApplication::setApplicationVersion("1.1");
    QApplication::setStyle("Fusion");
    
    BusinessProcessWindow window;
    window.show();
    
    return app.exec();
}

#include "main.moc"
