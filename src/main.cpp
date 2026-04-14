#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDebug>
#include <QtGlobal>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaType>

// Initialize Winsock
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "Core/StreamEngine.h"
#include "Render/VideoWidget.h"
#include "Common/RoomInfo.h"
#include "Common/Logger.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , centralWidget_(new QWidget(this))
        , verticalLayout_(new QVBoxLayout(centralWidget_))
        , controlLayout_(new QHBoxLayout())
        , modeTabWidget_(new QTabWidget())
        // Host tab
        , hostTab_(new QWidget())
        , hostLayout_(new QVBoxLayout(hostTab_))
        , hostControlLayout_(new QHBoxLayout())
        , roomNameEdit_(new QLineEdit(hostTab_))
        , startHostBtn_(new QPushButton("创建房间", hostTab_))
        , stopHostBtn_(new QPushButton("停止共享", hostTab_))
        , hostStatusLabel_(new QLabel("未共享", hostTab_))
        // Viewer tab
        , viewerTab_(new QWidget())
        , viewerLayout_(new QVBoxLayout(viewerTab_))
        , viewerControlLayout_(new QHBoxLayout())
        , refreshRoomsBtn_(new QPushButton("刷新房间", viewerTab_))
        , joinRoomBtn_(new QPushButton("加入房间", viewerTab_))
        , leaveRoomBtn_(new QPushButton("离开房间", viewerTab_))
        , roomsTreeWidget_(new QTreeWidget(viewerTab_))
        , viewerStatusLabel_(new QLabel("未连接", viewerTab_))
        // Video
        , videoWidget_(new lancast::VideoWidget())
        , latencyLabel_(new QLabel("延迟: -- ms"))
        // Menu
        , menuFile_(new QMenu("文件", this))
        , actionExit_(new QAction("退出", this))
    {
        setupUi();

        qRegisterMetaType<lancast::VideoFramePtr>("lancast::VideoFramePtr");
        qRegisterMetaType<lancast::RoomInfo>("lancast::RoomInfo");
        qRegisterMetaType<lancast::RoomInfoList>("std::vector<lancast::RoomInfo>");
        qRegisterMetaType<std::string>("std::string");

        // Initialize StreamEngine
        engine_ = std::make_shared<lancast::StreamEngine>();

        setupConnections();

        // Connect StreamEngine signals
        connect(engine_.get(), &lancast::StreamEngine::error,
            this, &MainWindow::onError);
        connect(engine_.get(), &lancast::StreamEngine::roomsUpdated,
            this, &MainWindow::onRoomsUpdated);

        // Menu
        menuBar()->addMenu(menuFile_);
        menuFile_->addAction(actionExit_);
        connect(actionExit_, &QAction::triggered, this, &QMainWindow::close);

        setWindowTitle("LanCast - 局域网屏幕共享");
        resize(1024, 768);
    }

private:
    void setupUi() {
        setCentralWidget(centralWidget_);

        // Room name edit
        roomNameEdit_->setPlaceholderText("输入房间名称");
        hostControlLayout_->addWidget(roomNameEdit_);

        // Host buttons
        stopHostBtn_->setEnabled(false);
        hostControlLayout_->addWidget(startHostBtn_);
        hostControlLayout_->addWidget(stopHostBtn_);

        hostLayout_->addLayout(hostControlLayout_);
        hostLayout_->addWidget(hostStatusLabel_);

        // Host tab
        modeTabWidget_->addTab(hostTab_, "共享屏幕 (Host)");

        // Viewer controls
        viewerControlLayout_->addWidget(refreshRoomsBtn_);
        viewerControlLayout_->addWidget(joinRoomBtn_);
        viewerControlLayout_->addWidget(leaveRoomBtn_);

        // Tree widget columns
        QStringList headers;
        headers << "房间ID" << "房间名称" << "主机" << "IP";
        roomsTreeWidget_->setHeaderLabels(headers);
        joinRoomBtn_->setEnabled(false);
        leaveRoomBtn_->setEnabled(false);

        viewerLayout_->addLayout(viewerControlLayout_);
        viewerLayout_->addWidget(roomsTreeWidget_);
        viewerLayout_->addWidget(viewerStatusLabel_);

        // Viewer tab
        modeTabWidget_->addTab(viewerTab_, "观看屏幕 (Viewer)");

        // Add controls
        controlLayout_->addWidget(modeTabWidget_);
        verticalLayout_->addLayout(controlLayout_);

        // Video widget
        videoWidget_->setMinimumSize(640, 480);
        verticalLayout_->addWidget(videoWidget_);

        // Latency label
        verticalLayout_->addWidget(latencyLabel_);
    }

    void setupConnections() {
        connect(startHostBtn_, &QPushButton::clicked, this, &MainWindow::onStartHost);
        connect(stopHostBtn_, &QPushButton::clicked, this, &MainWindow::onStopHost);
        connect(refreshRoomsBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshRooms);
        connect(joinRoomBtn_, &QPushButton::clicked, this, &MainWindow::onJoinRoom);
        connect(leaveRoomBtn_, &QPushButton::clicked, this, &MainWindow::onLeaveRoom);
        connect(roomsTreeWidget_, &QTreeWidget::itemSelectionChanged,
                this, &MainWindow::onRoomSelectionChanged);

        // Video frame display
        connect(engine_.get(), &lancast::StreamEngine::newVideoFrame,
            this,
            [this](const lancast::VideoFramePtr& frame) {
                    videoWidget_->displayFrame(frame);
                },
            Qt::DirectConnection);
    }

private slots:
    void onStartHost() {
        QString roomName = roomNameEdit_->text();
        if (roomName.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入房间名称");
            return;
        }

        if (engine_->startHost(roomName.toStdString())) {
            startHostBtn_->setEnabled(false);
            stopHostBtn_->setEnabled(true);
            roomNameEdit_->setEnabled(false);
            hostStatusLabel_->setText("正在共享: " + roomName);
            modeTabWidget_->setTabEnabled(1, false);
        } else {
            QMessageBox::critical(this, "错误", "启动共享失败");
        }
    }

    void onStopHost() {
        engine_->stopHost();
        startHostBtn_->setEnabled(true);
        stopHostBtn_->setEnabled(false);
        roomNameEdit_->setEnabled(true);
        hostStatusLabel_->setText("未共享");
        modeTabWidget_->setTabEnabled(1, true);
        videoWidget_->clear();
    }

    void onRefreshRooms() {
        auto discovery = engine_->discovery();
        if (discovery) {
            discovery->sendQuery();
            auto rooms = discovery->getRooms();
            updateRoomList(rooms);
        }
    }

    void onJoinRoom() {
        auto selected = roomsTreeWidget_->currentItem();
        if (!selected) return;

        QString roomId = selected->text(0);

        auto rooms = engine_->discovery()->getRooms();
        for (const auto& room : rooms) {
            if (room.room_id_ == roomId.toStdString()) {
                if (engine_->startViewer(room)) {
                    joinRoomBtn_->setEnabled(false);
                    leaveRoomBtn_->setEnabled(true);
                    refreshRoomsBtn_->setEnabled(false);
                    viewerStatusLabel_->setText("已连接: " + QString::fromStdString(room.room_name_));
                    modeTabWidget_->setTabEnabled(0, false);
                }
                break;
            }
        }
    }

    void onLeaveRoom() {
        engine_->stopViewer();
        joinRoomBtn_->setEnabled(true);
        leaveRoomBtn_->setEnabled(false);
        refreshRoomsBtn_->setEnabled(true);
        viewerStatusLabel_->setText("未连接");
        modeTabWidget_->setTabEnabled(0, true);
        videoWidget_->clear();
    }

    void onRoomSelectionChanged() {
        joinRoomBtn_->setEnabled(roomsTreeWidget_->currentItem() != nullptr);
    }

    void onNewVideoFrame(const lancast::VideoFramePtr& frame) {
        Q_UNUSED(frame);
        // Frame is displayed via lambda connection above
    }

    void onError(const std::string& message) {
        QMessageBox::critical(this, "错误", QString::fromStdString(message));
    }

    void onRoomsUpdated(const std::vector<lancast::RoomInfo>& rooms) {
        updateRoomList(rooms);
    }

private:
    void updateRoomList(const std::vector<lancast::RoomInfo>& rooms) {
        roomsTreeWidget_->clear();
        for (const auto& room : rooms) {
            auto item = new QTreeWidgetItem();
            item->setText(0, QString::fromStdString(room.room_id_));
            item->setText(1, QString::fromStdString(room.room_name_));
            item->setText(2, QString::fromStdString(room.host_name_));
            item->setText(3, QString::fromStdString(room.host_ip_));
            roomsTreeWidget_->addTopLevelItem(item);
        }
        joinRoomBtn_->setEnabled(roomsTreeWidget_->currentItem() != nullptr);
    }

    // UI elements
    QWidget* centralWidget_;
    QVBoxLayout* verticalLayout_;
    QHBoxLayout* controlLayout_;
    QTabWidget* modeTabWidget_;

    // Host tab
    QWidget* hostTab_;
    QVBoxLayout* hostLayout_;
    QHBoxLayout* hostControlLayout_;
    QLineEdit* roomNameEdit_;
    QPushButton* startHostBtn_;
    QPushButton* stopHostBtn_;
    QLabel* hostStatusLabel_;

    // Viewer tab
    QWidget* viewerTab_;
    QVBoxLayout* viewerLayout_;
    QHBoxLayout* viewerControlLayout_;
    QPushButton* refreshRoomsBtn_;
    QPushButton* joinRoomBtn_;
    QPushButton* leaveRoomBtn_;
    QTreeWidget* roomsTreeWidget_;
    QLabel* viewerStatusLabel_;

    // Video
    lancast::VideoWidget* videoWidget_;
    QLabel* latencyLabel_;

    // Engine
    lancast::StreamEnginePtr engine_;

    // Menu
    QMenu* menuFile_;
    QAction* actionExit_;
};

int main(int argc, char* argv[]) {
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    QApplication app(argc, argv);

    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QString logPath = logDir + QString("/lancast-%1.log").arg(static_cast<qulonglong>(GetCurrentProcessId()));
    lancast::Logger::setLogFile(logPath.toStdString());
    lancast::Logger::log(std::string("process started, log file=") + logPath.toStdString());
    app.setApplicationName("LanCast");
    app.setApplicationVersion("1.0.0");

    MainWindow w;
    w.show();

    int ret = app.exec();

    WSACleanup();
    return ret;
}

#include "main.moc"
