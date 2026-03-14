#include "app/main_window.h"

#include "layout/layout_serializer.h"
#include "sd_direct_types.h"
#include "widgets/playback_timeline_widget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMetaObject>
#include <QPalette>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QStatusBar>
#include <QToolButton>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWidget>

#include <QtWidgets/QDockWidget>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtCore/QJsonArray>
#include <QtCore/QPoint>

#include <chrono>
#include <limits>

namespace
{
    QString FormatReplayTimeUs(std::int64_t timeUs)
    {
        const std::int64_t clampedUs = std::max<std::int64_t>(0, timeUs);
        const std::int64_t totalMs = clampedUs / 1000;
        const std::int64_t totalSeconds = totalMs / 1000;
        const std::int64_t minutes = totalSeconds / 60;
        const std::int64_t seconds = totalSeconds % 60;
        const std::int64_t ms = totalMs % 1000;
        return QString("%1:%2.%3").arg(minutes).arg(seconds, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));
    }

    QString MarkerKindLabel(sd::transport::PlaybackMarkerKind kind)
    {
        switch (kind)
        {
            case sd::transport::PlaybackMarkerKind::Connect:
                return "connect";
            case sd::transport::PlaybackMarkerKind::Disconnect:
                return "disconnect";
            case sd::transport::PlaybackMarkerKind::Stale:
                return "stale";
            case sd::transport::PlaybackMarkerKind::Anomaly:
                return "anomaly";
            case sd::transport::PlaybackMarkerKind::Generic:
            default:
                return "marker";
        }
    }

    QString FormatReplaySpanUs(std::int64_t spanUs)
    {
        return QString("%1s").arg(static_cast<double>(std::max<std::int64_t>(0, spanUs)) / 1000000.0, 0, 'f', 3);
    }

    sd::widgets::VariableType ToVariableType(int valueType)
    {
        switch (valueType)
        {
            case static_cast<int>(sd::direct::ValueType::Bool):
                return sd::widgets::VariableType::Bool;
            case static_cast<int>(sd::direct::ValueType::Double):
                return sd::widgets::VariableType::Double;
            case static_cast<int>(sd::direct::ValueType::String):
                return sd::widgets::VariableType::String;
            default:
                return sd::widgets::VariableType::String;
        }
    }

    sd::widgets::VariableType ToVariableTypeFromWidgetType(const QString& widgetType)
    {
        if (widgetType.startsWith("bool."))
        {
            return sd::widgets::VariableType::Bool;
        }
        if (widgetType.startsWith("double."))
        {
            return sd::widgets::VariableType::Double;
        }

        return sd::widgets::VariableType::String;
    }

    void ApplyLayoutEntryToTile(sd::widgets::VariableTile* tile, const sd::layout::WidgetLayoutEntry& entry)
    {
        if (tile == nullptr)
        {
            return;
        }

        tile->setGeometry(entry.geometry);
        if (!entry.widgetType.isEmpty())
        {
            tile->SetWidgetType(entry.widgetType);
        }

        if (entry.gaugeLowerLimit.isValid())
        {
            tile->SetGaugeProperties(
                entry.gaugeLowerLimit.toDouble(),
                entry.gaugeUpperLimit.isValid() ? entry.gaugeUpperLimit.toDouble() : 1.0,
                entry.gaugeTickInterval.isValid() ? entry.gaugeTickInterval.toDouble() : 0.2,
                entry.gaugeShowTickMarks.isValid() ? entry.gaugeShowTickMarks.toBool() : true
            );
        }

        if (entry.progressBarLowerLimit.isValid())
        {
            tile->SetProgressBarProperties(
                entry.progressBarLowerLimit.toDouble(),
                entry.progressBarUpperLimit.isValid() ? entry.progressBarUpperLimit.toDouble() : 1.0
            );
        }

        if (entry.progressBarShowPercentage.isValid())
        {
            tile->SetProgressBarShowPercentage(entry.progressBarShowPercentage.toBool());
        }

        if (entry.progressBarForegroundColor.isValid() || entry.progressBarBackgroundColor.isValid())
        {
            tile->SetProgressBarColors(
                entry.progressBarForegroundColor.isValid() ? entry.progressBarForegroundColor.toString() : "",
                entry.progressBarBackgroundColor.isValid() ? entry.progressBarBackgroundColor.toString() : ""
            );
        }

        if (entry.sliderLowerLimit.isValid())
        {
            tile->SetSliderProperties(
                entry.sliderLowerLimit.toDouble(),
                entry.sliderUpperLimit.isValid() ? entry.sliderUpperLimit.toDouble() : 1.0,
                entry.sliderTickInterval.isValid() ? entry.sliderTickInterval.toDouble() : 0.2,
                entry.sliderShowTickMarks.isValid() ? entry.sliderShowTickMarks.toBool() : true
            );
        }

        if (entry.linePlotBufferSizeSamples.isValid())
        {
            tile->SetLinePlotProperties(
                entry.linePlotBufferSizeSamples.toInt(),
                entry.linePlotAutoYAxis.isValid() ? entry.linePlotAutoYAxis.toBool() : true,
                entry.linePlotYLowerLimit.isValid() ? entry.linePlotYLowerLimit.toDouble() : 0.0,
                entry.linePlotYUpperLimit.isValid() ? entry.linePlotYUpperLimit.toDouble() : 1.0
            );
        }

        if (entry.linePlotShowNumberLines.isValid())
        {
            tile->SetLinePlotNumberLinesVisible(entry.linePlotShowNumberLines.toBool());
        }
        if (entry.linePlotShowGridLines.isValid())
        {
            tile->SetLinePlotGridLinesVisible(entry.linePlotShowGridLines.toBool());
        }

        if (entry.doubleNumericEditable.isValid())
        {
            tile->SetDoubleNumericEditable(entry.doubleNumericEditable.toBool());
        }

        if (entry.textFontPointSize.isValid())
        {
            tile->SetTextFontPointSize(entry.textFontPointSize.toInt());
        }

        if (entry.boolCheckboxShowLabel.isValid())
        {
            tile->SetBoolCheckboxShowLabel(entry.boolCheckboxShowLabel.toBool());
        }

        if (entry.boolValue.isValid())
        {
            tile->SetBoolValue(entry.boolValue.toBool());
        }
        if (entry.doubleValue.isValid())
        {
            tile->SetDoubleValue(entry.doubleValue.toDouble());
        }
        if (entry.stringValue.isValid())
        {
            tile->SetStringValue(entry.stringValue.toString());
        }
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    RefreshWindowTitle();
    resize(1200, 800);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName("dashboardCanvas");
    m_canvas->setAutoFillBackground(true);
    m_canvas->setBackgroundRole(QPalette::Window);
    setCentralWidget(m_canvas);

    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* saveLayoutAction = fileMenu->addAction("Save / Update Layout");
    QAction* saveLayoutAsAction = fileMenu->addAction("Save Layout As...");
    QAction* loadLayoutAction = fileMenu->addAction("Load Layout (Merge)");
    QAction* loadLayoutReplaceAction = fileMenu->addAction("Load Layout (Replace)");
    QAction* importLegacyXmlAction = fileMenu->addAction("Import Legacy XML...");
    m_openReplayFileAction = fileMenu->addAction("Replay: Open session file...");
    fileMenu->addSeparator();
    QAction* clearWidgetsAction = fileMenu->addAction("Clear Widgets");
    connect(saveLayoutAction, &QAction::triggered, this, &MainWindow::OnSaveLayout);
    connect(saveLayoutAsAction, &QAction::triggered, this, &MainWindow::OnSaveLayoutAs);
    connect(loadLayoutAction, &QAction::triggered, this, &MainWindow::OnLoadLayout);
    connect(loadLayoutReplaceAction, &QAction::triggered, this, &MainWindow::OnLoadLayoutReplace);
    connect(importLegacyXmlAction, &QAction::triggered, this, &MainWindow::OnImportLegacyXmlLayout);
    connect(m_openReplayFileAction, &QAction::triggered, this, &MainWindow::OnOpenReplayFile);
    connect(clearWidgetsAction, &QAction::triggered, this, &MainWindow::OnClearWidgets);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    m_editableAction = viewMenu->addAction("Editable");
    m_editableAction->setCheckable(true);
    connect(m_editableAction, &QAction::triggered, this, &MainWindow::OnToggleEditable);

    m_snapToGridAction = viewMenu->addAction("Snap to grid (8px)");
    m_snapToGridAction->setCheckable(true);
    m_snapToGridAction->setChecked(m_snapToGrid);
    connect(m_snapToGridAction, &QAction::triggered, this, &MainWindow::OnToggleSnapToGrid);

    QMenu* interactionMenu = viewMenu->addMenu("Editable interaction");
    m_moveModeAction = interactionMenu->addAction("Move only");
    m_moveModeAction->setCheckable(true);
    m_resizeModeAction = interactionMenu->addAction("Resize only");
    m_resizeModeAction->setCheckable(true);
    m_moveResizeModeAction = interactionMenu->addAction("Move and resize");
    m_moveResizeModeAction->setCheckable(true);

    connect(m_moveModeAction, &QAction::triggered, this, &MainWindow::OnSetMoveMode);
    connect(m_resizeModeAction, &QAction::triggered, this, &MainWindow::OnSetResizeMode);
    connect(m_moveResizeModeAction, &QAction::triggered, this, &MainWindow::OnSetMoveResizeMode);

    m_moveModeAction->setChecked(false);
    m_resizeModeAction->setChecked(false);
    m_moveResizeModeAction->setChecked(true);

    viewMenu->addSeparator();
    m_telemetryFeatureViewAction = viewMenu->addAction("Enable telemetry recording/playback UI");
    m_telemetryFeatureViewAction->setCheckable(true);
    m_telemetryFeatureViewAction->setChecked(true);

    m_replayMarkersViewAction = viewMenu->addAction("Replay Markers");
    m_replayMarkersViewAction->setCheckable(true);
    m_replayMarkersViewAction->setChecked(true);
    m_replayMarkersViewAction->setEnabled(false);

    m_replayControlsViewAction = viewMenu->addAction("Replay Controls");
    m_replayControlsViewAction->setCheckable(true);
    m_replayControlsViewAction->setChecked(true);
    m_replayControlsViewAction->setEnabled(false);

    m_replayTimelineViewAction = viewMenu->addAction("Replay Timeline");
    m_replayTimelineViewAction->setCheckable(true);
    m_replayTimelineViewAction->setChecked(true);
    m_replayTimelineViewAction->setEnabled(false);

    m_statusLabel = new QLabel("State: Disconnected", this);
    statusBar()->addPermanentWidget(m_statusLabel);

    QMenu* connectionMenu = menuBar()->addMenu("&Connection");
    m_connectTransportAction = connectionMenu->addAction("Connect");
    m_disconnectTransportAction = connectionMenu->addAction("Disconnect");
    connectionMenu->addSeparator();
    m_useDirectTransportAction = connectionMenu->addAction("Use Direct transport");
    m_useDirectTransportAction->setCheckable(true);
    m_useNetworkTablesTransportAction = connectionMenu->addAction("Use NetworkTables transport");
    m_useNetworkTablesTransportAction->setCheckable(true);
    m_useReplayTransportAction = connectionMenu->addAction("Use Replay transport");
    m_useReplayTransportAction->setCheckable(true);
    connectionMenu->addSeparator();
    m_ntUseTeamAction = connectionMenu->addAction("NT: Use team number");
    m_ntUseTeamAction->setCheckable(true);
    QAction* ntSetHostAction = connectionMenu->addAction("NT: Set host...");
    QAction* ntSetTeamAction = connectionMenu->addAction("NT: Set team...");

    connect(m_connectTransportAction, &QAction::triggered, this, &MainWindow::OnConnectTransport);
    connect(m_disconnectTransportAction, &QAction::triggered, this, &MainWindow::OnDisconnectTransport);
    connect(m_useDirectTransportAction, &QAction::triggered, this, &MainWindow::OnUseDirectTransport);
    connect(m_useNetworkTablesTransportAction, &QAction::triggered, this, &MainWindow::OnUseNetworkTablesTransport);
    connect(m_useReplayTransportAction, &QAction::triggered, this, &MainWindow::OnUseReplayTransport);
    connect(m_telemetryFeatureViewAction, &QAction::triggered, this, &MainWindow::OnToggleTelemetryFeature);
    connect(m_ntUseTeamAction, &QAction::triggered, this, &MainWindow::OnToggleNtUseTeam);
    connect(ntSetHostAction, &QAction::triggered, this, &MainWindow::OnSetNtHost);
    connect(ntSetTeamAction, &QAction::triggered, this, &MainWindow::OnSetNtTeam);

    m_telemetryControlsPanel = new QWidget(this);
    auto* playbackLayout = new QHBoxLayout(m_telemetryControlsPanel);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    playbackLayout->setSpacing(6);

    auto* playbackLabel = new QLabel("Telemetry", m_telemetryControlsPanel);
    playbackLayout->addWidget(playbackLabel);

    m_recordButton = new QPushButton(QString::fromUtf8("\xE2\x97\x8F"), m_telemetryControlsPanel);
    m_recordButton->setToolTip("Record telemetry");
    m_recordButton->setCheckable(true);
    m_recordButton->setChecked(false);
    m_recordButton->setFixedSize(24, 24);
    m_recordButton->setStyleSheet(
        "QPushButton { color: #8a8a8a; font-weight: 900; }"
        "QPushButton:checked { color: #ff2b2b; border: 1px solid #9b1b1b; background-color: #221616; }"
        "QPushButton:disabled { color: #595959; border-color: #3f3f3f; }"
    );
    playbackLayout->addWidget(m_recordButton);

    m_rewindButton = new QToolButton(m_telemetryControlsPanel);
    m_rewindButton->setText(QString::fromUtf8("|\xE2\x97\x80"));
    m_rewindButton->setToolTip("Rewind to beginning");
    m_rewindButton->setFixedSize(24, 24);
    m_rewindButton->setStyleSheet(
        "QToolButton { color: #8e9aaf; font-weight: 700; }"
        "QToolButton:disabled { color: #5a5a5a; }"
    );
    playbackLayout->addWidget(m_rewindButton);

    m_playPauseButton = new QToolButton(m_telemetryControlsPanel);
    m_playPauseButton->setText(QString::fromUtf8("\xE2\x96\xB6"));
    m_playPauseButton->setToolTip("Play/Pause telemetry replay");
    m_playPauseButton->setFixedSize(24, 24);
    m_playPauseButton->setStyleSheet("QToolButton { color: #2f9e44; font-weight: 700; }");
    playbackLayout->addWidget(m_playPauseButton);

    m_prevMarkerButton = new QToolButton(m_telemetryControlsPanel);
    m_prevMarkerButton->setText(QString::fromUtf8("\xE2\x8F\xAE"));
    m_prevMarkerButton->setToolTip("Jump to previous marker");
    m_prevMarkerButton->setFixedSize(24, 24);
    m_prevMarkerButton->setStyleSheet(
        "QToolButton { color: #8e9aaf; font-weight: 700; }"
        "QToolButton:disabled { color: #5a5a5a; }"
    );
    playbackLayout->addWidget(m_prevMarkerButton);

    m_nextMarkerButton = new QToolButton(m_telemetryControlsPanel);
    m_nextMarkerButton->setText(QString::fromUtf8("\xE2\x8F\xAD"));
    m_nextMarkerButton->setToolTip("Jump to next marker");
    m_nextMarkerButton->setFixedSize(24, 24);
    m_nextMarkerButton->setStyleSheet(
        "QToolButton { color: #8e9aaf; font-weight: 700; }"
        "QToolButton:disabled { color: #5a5a5a; }"
    );
    playbackLayout->addWidget(m_nextMarkerButton);

    m_addBookmarkButton = new QToolButton(m_telemetryControlsPanel);
    m_addBookmarkButton->setText("B+");
    m_addBookmarkButton->setToolTip("Add bookmark at current replay time");
    m_addBookmarkButton->setFixedSize(28, 24);
    m_addBookmarkButton->setStyleSheet(
        "QToolButton { color: #6cb6ff; font-weight: 700; }"
        "QToolButton:disabled { color: #5a5a5a; }"
    );
    playbackLayout->addWidget(m_addBookmarkButton);

    m_clearBookmarksButton = new QToolButton(m_telemetryControlsPanel);
    m_clearBookmarksButton->setText("Bx");
    m_clearBookmarksButton->setToolTip("Clear user bookmarks");
    m_clearBookmarksButton->setFixedSize(28, 24);
    m_clearBookmarksButton->setStyleSheet(
        "QToolButton { color: #d98f8f; font-weight: 700; }"
        "QToolButton:disabled { color: #5a5a5a; }"
    );
    playbackLayout->addWidget(m_clearBookmarksButton);

    m_playbackRateCombo = new QComboBox(m_telemetryControlsPanel);
    m_playbackRateCombo->addItem("0.25x", 0.25);
    m_playbackRateCombo->addItem("0.5x", 0.5);
    m_playbackRateCombo->addItem("1x", 1.0);
    m_playbackRateCombo->addItem("2x", 2.0);
    m_playbackRateCombo->setCurrentIndex(2);
    playbackLayout->addWidget(m_playbackRateCombo);

    m_replayControlsDock = new QDockWidget("Replay Controls", this);
    m_replayControlsDock->setObjectName("replayControlsDock");
    m_replayControlsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_replayControlsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_replayControlsDock->setContextMenuPolicy(Qt::CustomContextMenu);
    m_replayControlsDock->setWidget(m_telemetryControlsPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_replayControlsDock);
    connect(m_recordButton, &QPushButton::toggled, this, &MainWindow::OnRecordToggled);
    connect(m_rewindButton, &QToolButton::clicked, this, &MainWindow::OnPlaybackRewindToStart);
    connect(m_playPauseButton, &QToolButton::clicked, this, &MainWindow::OnPlaybackPlayPause);
    connect(m_prevMarkerButton, &QToolButton::clicked, this, &MainWindow::OnPlaybackPreviousMarker);
    connect(m_nextMarkerButton, &QToolButton::clicked, this, &MainWindow::OnPlaybackNextMarker);
    connect(m_addBookmarkButton, &QToolButton::clicked, this, &MainWindow::OnAddReplayBookmark);
    connect(m_clearBookmarksButton, &QToolButton::clicked, this, &MainWindow::OnClearReplayBookmarks);
    connect(m_playbackRateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::OnPlaybackRateChanged);

    m_playbackTimeline = new sd::widgets::PlaybackTimelineWidget(this);
    m_playbackTimeline->setMinimumWidth(260);
    m_playbackTimeline->setToolTip("Telemetry timeline (left-drag scrub, wheel zoom, right-drag pan)");
    m_replayTimelineDock = new QDockWidget("Replay Timeline", this);
    m_replayTimelineDock->setObjectName("replayTimelineDock");
    m_replayTimelineDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_replayTimelineDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_replayTimelineDock->setContextMenuPolicy(Qt::CustomContextMenu);
    m_replayTimelineDock->setWidget(m_playbackTimeline);
    addDockWidget(Qt::BottomDockWidgetArea, m_replayTimelineDock);
    splitDockWidget(m_replayControlsDock, m_replayTimelineDock, Qt::Horizontal);
    connect(m_playbackTimeline, &sd::widgets::PlaybackTimelineWidget::CursorScrubbedUs, this, &MainWindow::OnPlaybackCursorScrubbed);

    connect(
        m_replayControlsDock,
        &QWidget::customContextMenuRequested,
        this,
        [this](const QPoint& pos)
        {
            if (m_replayControlsDock == nullptr)
            {
                return;
            }

            QMenu menu(this);
            QAction* floatAction = menu.addAction("Float");
            floatAction->setCheckable(true);
            floatAction->setChecked(m_replayControlsDock->isFloating());

            menu.addSeparator();
            QAction* dockRightAction = menu.addAction("Dock Right");
            QAction* dockLeftAction = menu.addAction("Dock Left");
            QAction* dockBottomAction = menu.addAction("Dock Bottom");

            QAction* chosen = menu.exec(m_replayControlsDock->mapToGlobal(pos));
            if (chosen == nullptr)
            {
                return;
            }

            if (chosen == floatAction)
            {
                m_replayControlsDock->setFloating(!m_replayControlsDock->isFloating());
                m_replayControlsDock->show();
                return;
            }

            m_replayControlsDock->setFloating(false);
            if (chosen == dockRightAction)
            {
                addDockWidget(Qt::RightDockWidgetArea, m_replayControlsDock);
            }
            else if (chosen == dockLeftAction)
            {
                addDockWidget(Qt::LeftDockWidgetArea, m_replayControlsDock);
            }
            else if (chosen == dockBottomAction)
            {
                addDockWidget(Qt::BottomDockWidgetArea, m_replayControlsDock);
                if (m_replayTimelineDock != nullptr)
                {
                    m_replayTimelineDock->setFloating(false);
                    addDockWidget(Qt::BottomDockWidgetArea, m_replayTimelineDock);
                    splitDockWidget(m_replayControlsDock, m_replayTimelineDock, Qt::Horizontal);
                }
            }
            m_replayControlsDock->show();
        }
    );

    connect(
        m_replayTimelineDock,
        &QWidget::customContextMenuRequested,
        this,
        [this](const QPoint& pos)
        {
            if (m_replayTimelineDock == nullptr)
            {
                return;
            }

            QMenu menu(this);
            QAction* floatAction = menu.addAction("Float");
            floatAction->setCheckable(true);
            floatAction->setChecked(m_replayTimelineDock->isFloating());

            menu.addSeparator();
            QAction* dockRightAction = menu.addAction("Dock Right");
            QAction* dockLeftAction = menu.addAction("Dock Left");
            QAction* dockBottomAction = menu.addAction("Dock Bottom");

            QAction* chosen = menu.exec(m_replayTimelineDock->mapToGlobal(pos));
            if (chosen == nullptr)
            {
                return;
            }

            if (chosen == floatAction)
            {
                m_replayTimelineDock->setFloating(!m_replayTimelineDock->isFloating());
                m_replayTimelineDock->show();
                return;
            }

            m_replayTimelineDock->setFloating(false);
            if (chosen == dockRightAction)
            {
                addDockWidget(Qt::RightDockWidgetArea, m_replayTimelineDock);
            }
            else if (chosen == dockLeftAction)
            {
                addDockWidget(Qt::LeftDockWidgetArea, m_replayTimelineDock);
            }
            else if (chosen == dockBottomAction)
            {
                addDockWidget(Qt::BottomDockWidgetArea, m_replayTimelineDock);
                if (m_replayControlsDock != nullptr)
                {
                    m_replayControlsDock->setFloating(false);
                    addDockWidget(Qt::BottomDockWidgetArea, m_replayControlsDock);
                    splitDockWidget(m_replayControlsDock, m_replayTimelineDock, Qt::Horizontal);
                }
            }
            m_replayTimelineDock->show();
        }
    );

    connect(
        m_replayControlsDock,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            if (m_syncingReplayControlsDockVisibility)
            {
                return;
            }
            m_replayControlsPreferredVisible = visible;
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/controlsVisible", m_replayControlsPreferredVisible);
        }
    );
    connect(
        m_replayTimelineDock,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            if (m_syncingReplayTimelineDockVisibility)
            {
                return;
            }
            m_replayTimelinePreferredVisible = visible;
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/timelineVisible", m_replayTimelinePreferredVisible);
        }
    );
    connect(
        m_replayControlsViewAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            m_replayControlsPreferredVisible = checked;
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/controlsVisible", m_replayControlsPreferredVisible);
            if (m_replayControlsDock != nullptr)
            {
                m_syncingReplayControlsDockVisibility = true;
                m_replayControlsDock->setVisible(checked);
                m_syncingReplayControlsDockVisibility = false;
            }
        }
    );
    connect(
        m_replayTimelineViewAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            m_replayTimelinePreferredVisible = checked;
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/timelineVisible", m_replayTimelinePreferredVisible);
            if (m_replayTimelineDock != nullptr)
            {
                m_syncingReplayTimelineDockVisibility = true;
                m_replayTimelineDock->setVisible(checked);
                m_syncingReplayTimelineDockVisibility = false;
            }
        }
    );

    m_replayMarkerDock = new QDockWidget("Replay Markers", this);
    m_replayMarkerDock->setObjectName("replayMarkerDock");
    m_replayMarkerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_replayMarkerDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_replayMarkerDock->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* markerPanel = new QWidget(m_replayMarkerDock);
    auto* markerPanelLayout = new QVBoxLayout(markerPanel);
    markerPanelLayout->setContentsMargins(4, 4, 4, 4);
    markerPanelLayout->setSpacing(4);

    m_replayMarkerList = new QListWidget(markerPanel);
    m_replayMarkerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_replayMarkerList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_replayMarkerList->setMouseTracking(false);
    m_replayMarkerList->setAlternatingRowColors(true);
    markerPanelLayout->addWidget(m_replayMarkerList, 1);

    m_replaySelectionSummaryLabel = new QLabel("Window: 0 markers", markerPanel);
    m_replaySelectionSummaryLabel->setStyleSheet("QLabel { color: #8c8c8c; }");
    markerPanelLayout->addWidget(m_replaySelectionSummaryLabel);

    m_replayMarkerDock->setWidget(markerPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_replayMarkerDock);
    connect(
        m_replayMarkerDock,
        &QWidget::customContextMenuRequested,
        this,
        [this](const QPoint& pos)
        {
            if (m_replayMarkerDock == nullptr)
            {
                return;
            }

            QMenu menu(this);
            QAction* floatAction = menu.addAction("Float");
            floatAction->setCheckable(true);
            floatAction->setChecked(m_replayMarkerDock->isFloating());

            menu.addSeparator();
            QAction* dockRightAction = menu.addAction("Dock Right");
            QAction* dockLeftAction = menu.addAction("Dock Left");

            QAction* chosen = menu.exec(m_replayMarkerDock->mapToGlobal(pos));
            if (chosen == nullptr)
            {
                return;
            }

            if (chosen == floatAction)
            {
                m_replayMarkerDock->setFloating(!m_replayMarkerDock->isFloating());
                m_replayMarkerDock->show();
                return;
            }

            if (chosen == dockRightAction)
            {
                m_replayMarkerDock->setFloating(false);
                addDockWidget(Qt::RightDockWidgetArea, m_replayMarkerDock);
                m_replayMarkerDock->show();
                return;
            }

            if (chosen == dockLeftAction)
            {
                m_replayMarkerDock->setFloating(false);
                addDockWidget(Qt::LeftDockWidgetArea, m_replayMarkerDock);
                m_replayMarkerDock->show();
            }
        }
    );
    connect(
        m_replayMarkerDock,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            if (m_replayMarkerDock == nullptr || !m_replayMarkerDock->isEnabled())
            {
                return;
            }

            if (m_syncingReplayMarkerDockVisibility)
            {
                return;
            }

            if (!QCoreApplication::closingDown()
                && isVisible()
                && m_replayMarkersViewAction != nullptr
                && m_replayMarkersViewAction->isEnabled())
            {
                m_replayMarkersPreferredVisible = visible;
                QSettings settings("SmartDashboard", "SmartDashboardApp");
                settings.setValue("replay/markersVisible", m_replayMarkersPreferredVisible);
            }

            if (m_replayMarkersViewAction != nullptr)
            {
                const bool prior = m_replayMarkersViewAction->blockSignals(true);
                m_replayMarkersViewAction->setChecked(visible);
                m_replayMarkersViewAction->blockSignals(prior);
            }
        }
    );
    connect(
        m_replayMarkersViewAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            if (m_syncingReplayMarkerDockVisibility)
            {
                return;
            }

            if (m_replayMarkerDock != nullptr)
            {
                m_syncingReplayMarkerDockVisibility = true;
                m_replayMarkerDock->setVisible(checked);
                m_syncingReplayMarkerDockVisibility = false;
            }

            m_replayMarkersPreferredVisible = checked;
            if (!QCoreApplication::closingDown())
            {
                QSettings settings("SmartDashboard", "SmartDashboardApp");
                settings.setValue("replay/markersVisible", m_replayMarkersPreferredVisible);
            }
        }
    );
    connect(m_replayMarkerList, &QListWidget::itemActivated, this, &MainWindow::OnReplayMarkerActivated);
    connect(m_replayMarkerList, &QListWidget::itemClicked, this, &MainWindow::OnReplayMarkerActivated);

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    m_connectionConfig.kind = static_cast<sd::transport::TransportKind>(
        settings.value("connection/transportKind", static_cast<int>(sd::transport::TransportKind::Direct)).toInt()
    );
    m_connectionConfig.ntHost = settings.value("connection/ntHost", "127.0.0.1").toString();
    m_connectionConfig.ntTeam = settings.value("connection/ntTeam", 0).toInt();
    m_connectionConfig.ntUseTeam = settings.value("connection/ntUseTeam", true).toBool();
    m_connectionConfig.ntClientName = settings.value("connection/ntClientName", "SmartDashboardApp").toString();
    m_connectionConfig.replayFilePath = settings.value("connection/replayFilePath").toString();
    m_telemetryFeatureEnabled = settings.value("telemetry/enabled", true).toBool();
    m_recordRequested = settings.value("telemetry/recordEnabled", false).toBool();
    m_replayControlsPreferredVisible = settings.value("replay/controlsVisible", true).toBool();
    m_replayTimelinePreferredVisible = settings.value("replay/timelineVisible", true).toBool();
    m_replayMarkersPreferredVisible = settings.value("replay/markersVisible", true).toBool();
    if (m_telemetryFeatureViewAction != nullptr)
    {
        m_telemetryFeatureViewAction->setChecked(m_telemetryFeatureEnabled);
    }
    if (m_replayMarkersViewAction != nullptr)
    {
        m_replayMarkersViewAction->setChecked(m_replayMarkersPreferredVisible);
        m_replayMarkersViewAction->setEnabled(false);
    }
    if (m_replayControlsViewAction != nullptr)
    {
        m_replayControlsViewAction->setChecked(m_replayControlsPreferredVisible);
        m_replayControlsViewAction->setEnabled(false);
    }
    if (m_replayTimelineViewAction != nullptr)
    {
        m_replayTimelineViewAction->setChecked(m_replayTimelinePreferredVisible);
        m_replayTimelineViewAction->setEnabled(false);
    }
    if (m_recordButton != nullptr)
    {
        m_recordButton->setChecked(m_recordRequested);
    }

    LoadUserReplayBookmarks();
    ApplyTransportMenuChecks();

    m_playbackUiTimer = new QTimer(this);
    m_playbackUiTimer->setInterval(100);
    connect(m_playbackUiTimer, &QTimer::timeout, this, &MainWindow::UpdatePlaybackUiState);
    m_playbackUiTimer->start();
    UpdatePlaybackUiState();

    connect(
        qApp,
        &QCoreApplication::aboutToQuit,
        this,
        [this]()
        {
            // Persist top-level window geometry on shutdown.
            // Layout saving is handled by closeEvent() with unsaved-change prompt.
            SaveWindowGeometry();
        }
    );

    m_layoutFilePath = GetInitialLayoutPath();
    LoadLayoutFromPath(m_layoutFilePath, true);
    LoadWindowGeometry();
    StartTransport();
}

MainWindow::~MainWindow()
{
    StopTransport();
}

void MainWindow::OnToggleEditable()
{
    m_isEditable = m_editableAction->isChecked();
    for (auto& [_, tile] : m_tiles)
    {
        tile->SetEditable(m_isEditable);
        tile->SetSnapToGrid(m_snapToGrid, 8);
        tile->SetEditInteractionMode(m_editInteractionMode);
    }
}

void MainWindow::OnToggleSnapToGrid()
{
    m_snapToGrid = m_snapToGridAction->isChecked();
    for (auto& [_, tile] : m_tiles)
    {
        tile->SetSnapToGrid(m_snapToGrid, 8);
    }
}

void MainWindow::OnSetMoveMode()
{
    m_editInteractionMode = sd::widgets::EditInteractionMode::MoveOnly;
    m_moveModeAction->setChecked(true);
    m_resizeModeAction->setChecked(false);
    m_moveResizeModeAction->setChecked(false);

    for (auto& [_, tile] : m_tiles)
    {
        tile->SetEditInteractionMode(m_editInteractionMode);
    }
}

void MainWindow::OnSetResizeMode()
{
    m_editInteractionMode = sd::widgets::EditInteractionMode::ResizeOnly;
    m_moveModeAction->setChecked(false);
    m_resizeModeAction->setChecked(true);
    m_moveResizeModeAction->setChecked(false);

    for (auto& [_, tile] : m_tiles)
    {
        tile->SetEditInteractionMode(m_editInteractionMode);
    }
}

void MainWindow::OnSetMoveResizeMode()
{
    m_editInteractionMode = sd::widgets::EditInteractionMode::MoveAndResize;
    m_moveModeAction->setChecked(false);
    m_resizeModeAction->setChecked(false);
    m_moveResizeModeAction->setChecked(true);

    for (auto& [_, tile] : m_tiles)
    {
        tile->SetEditInteractionMode(m_editInteractionMode);
    }
}

void MainWindow::OnVariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq)
{
    // Sequence rollback detection: publisher restart resets seq,
    // so reset per-key sequence gating to accept new session updates.
    if (seq != 0 && m_lastTransportSeq != 0 && seq < m_lastTransportSeq)
    {
        m_variableStore.ResetSequenceTracking();
    }
    m_lastTransportSeq = seq;

    const sd::widgets::VariableType variableType = ToVariableType(valueType);
    const std::string keyStd = key.toStdString();
    const sd::model::VariableRecord& record = m_variableStore.Upsert(keyStd, variableType, value, seq);

    sd::widgets::VariableTile* tile = GetOrCreateTile(QString::fromStdString(record.key), record.type);
    if (tile == nullptr)
    {
        return;
    }

    switch (record.type)
    {
        case sd::widgets::VariableType::Bool:
            tile->SetBoolValue(record.value.toBool());
            break;
        case sd::widgets::VariableType::Double:
            tile->SetDoubleValue(record.value.toDouble());
            break;
        case sd::widgets::VariableType::String:
            tile->SetStringValue(record.value.toString());
            break;
        default:
            break;
    }

    RecordVariableEvent(key, valueType, value, seq);
}

void MainWindow::OnConnectionStateChanged(int state)
{
    m_connectionState = state;

    const int connected = static_cast<int>(sd::transport::ConnectionState::Connected);
    if (state == connected)
    {
        // Reconnect handling: reset sequence gating when transport re-enters connected state.
        m_variableStore.ResetSequenceTracking();
    }

    UpdateWindowConnectionText(state);
    RecordConnectionEvent(state);
}

void MainWindow::OnSaveLayout()
{
    if (!SaveLayoutUsingCurrentOrPrompt())
    {
        QMessageBox::warning(this, "Save Layout", "Failed to save layout.");
    }
}

void MainWindow::OnSaveLayoutAs()
{
    QString selected = QFileDialog::getSaveFileName(
        this,
        "Save Layout As",
        GetInitialLayoutPath(),
        "Layout Files (*.json);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return;
    }

    if (QFileInfo(selected).suffix().isEmpty())
    {
        selected += ".json";
    }

    if (!SaveLayoutToPath(selected))
    {
        QMessageBox::warning(this, "Save Layout As", "Failed to save layout.");
    }
}

void MainWindow::OnLoadLayout()
{
    const QString selected = QFileDialog::getOpenFileName(
        this,
        "Load Layout",
        GetInitialLayoutPath(),
        "Layout Files (*.json);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return;
    }

    if (!LoadLayoutFromPath(selected, true))
    {
        QMessageBox::warning(this, "Load Layout", "Failed to load layout.");
    }
}

void MainWindow::OnLoadLayoutReplace()
{
    const QString selected = QFileDialog::getOpenFileName(
        this,
        "Load Layout (Replace)",
        GetInitialLayoutPath(),
        "Layout Files (*.json);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return;
    }

    OnClearWidgets();
    if (!LoadLayoutFromPath(selected, true))
    {
        QMessageBox::warning(this, "Load Layout (Replace)", "Failed to load layout.");
    }
}

void MainWindow::OnImportLegacyXmlLayout()
{
    const QString selected = QFileDialog::getOpenFileName(
        this,
        "Import Legacy XML Layout",
        GetInitialLayoutPath(),
        "Legacy SmartDashboard XML (*.xml);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return;
    }

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    QStringList importIssues;
    if (!sd::layout::LoadLegacyXmlLayoutEntries(selected, entries, &importIssues))
    {
        QMessageBox::warning(this, "Import Legacy XML Layout", "Failed to import XML layout.");
        return;
    }

    m_suppressLayoutDirty = true;
    m_savedLayoutByKey.clear();
    for (const sd::layout::WidgetLayoutEntry& entry : entries)
    {
        m_savedLayoutByKey[entry.variableKey.toStdString()] = entry;
    }

    for (const sd::layout::WidgetLayoutEntry& entry : entries)
    {
        const sd::widgets::VariableType inferredType = ToVariableTypeFromWidgetType(entry.widgetType);
        sd::widgets::VariableTile* tile = GetOrCreateTile(entry.variableKey, inferredType);
        ApplyLayoutEntryToTile(tile, entry);
    }

    m_suppressLayoutDirty = false;
    m_layoutDirty = true;

    if (!importIssues.isEmpty())
    {
        QMessageBox issuePrompt(this);
        issuePrompt.setIcon(QMessageBox::Warning);
        issuePrompt.setWindowTitle("Import Legacy XML Layout");
        issuePrompt.setText("Layout imported with some unsupported legacy features.");
        issuePrompt.setDetailedText(importIssues.join("\n"));
        issuePrompt.exec();
    }
}

void MainWindow::OnClearWidgets()
{
    // Clear dashboard runtime state so repopulation behavior can be retested
    // without restarting the app process.
    for (auto& [_, tile] : m_tiles)
    {
        if (tile != nullptr)
        {
            tile->deleteLater();
        }
    }

    m_tiles.clear();
    m_savedLayoutByKey.clear();
    m_variableStore.Clear();
    m_nextTileOffset = 0;
    m_lastTransportSeq = 0;
    MarkLayoutDirty();
}

sd::widgets::VariableTile* MainWindow::GetOrCreateTile(const QString& key, sd::widgets::VariableType type)
{
    const std::string keyStd = key.toStdString();
    auto it = m_tiles.find(keyStd);
    if (it != m_tiles.end())
    {
        return it->second;
    }

    auto* tile = new sd::widgets::VariableTile(key, type, m_canvas);
    tile->SetTitleText(BuildDisplayLabel(key));
    connect(
        tile,
        &sd::widgets::VariableTile::ChangeWidgetRequested,
        this,
        [this, tile](const QString&, const QString& widgetType)
        {
            tile->setProperty("widgetType", widgetType);
            MarkLayoutDirty();
        }
    );
    connect(tile, &sd::widgets::VariableTile::ControlBoolEdited, this, &MainWindow::OnControlBoolEdited);
    connect(tile, &sd::widgets::VariableTile::ControlDoubleEdited, this, &MainWindow::OnControlDoubleEdited);
    connect(tile, &sd::widgets::VariableTile::ControlStringEdited, this, &MainWindow::OnControlStringEdited);
    connect(tile, &sd::widgets::VariableTile::RemoveRequested, this, &MainWindow::OnRemoveWidgetRequested);

    tile->setObjectName(QString("tile_%1").arg(QString::number(m_tiles.size() + 1)));
    tile->SetDefaultSize(QSize(220, 84));
    tile->SetEditable(m_isEditable);
    tile->SetSnapToGrid(m_snapToGrid, 8);
    tile->SetEditInteractionMode(m_editInteractionMode);

    auto savedIt = m_savedLayoutByKey.find(keyStd);
    if (savedIt != m_savedLayoutByKey.end())
    {
        const sd::layout::WidgetLayoutEntry& entry = savedIt->second;
        ApplyLayoutEntryToTile(tile, entry);
    }
    else
    {
        tile->setGeometry(24 + m_nextTileOffset, 32 + m_nextTileOffset, 220, 84);
        m_nextTileOffset = (m_nextTileOffset + 24) % 200;
    }

    tile->setProperty("variableKey", key);
    tile->setProperty("widgetType", tile->GetWidgetType());
    tile->installEventFilter(this);
    tile->show();

    m_tiles.emplace(keyStd, tile);

    if (!m_suppressLayoutDirty)
    {
        MarkLayoutDirty();
    }

    return tile;
}

QString MainWindow::BuildDisplayLabel(const QString& key) const
{
    if (
        m_connectionConfig.kind != sd::transport::TransportKind::Direct
        && m_connectionConfig.kind != sd::transport::TransportKind::Replay
    )
    {
        return key;
    }

    const QStringList segments = key.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty())
    {
        return key;
    }

    return segments.back();
}

void MainWindow::UpdateWindowConnectionText(int state)
{
    m_connectionState = state;
    RefreshWindowTitle();

    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        m_statusLabel->setText("Replay");
        return;
    }

    QString stateText = "Disconnected";
    if (state == static_cast<int>(sd::transport::ConnectionState::Connecting))
    {
        stateText = "Connecting";
    }
    else if (state == static_cast<int>(sd::transport::ConnectionState::Connected))
    {
        stateText = "Connected";
    }
    else if (state == static_cast<int>(sd::transport::ConnectionState::Stale))
    {
        stateText = "Stale";
    }

    m_statusLabel->setText(QString("State: %1").arg(stateText));
}

void MainWindow::LoadWindowGeometry()
{
    // Restore persisted main-window geometry/state (Qt settings-backed Memento pattern).
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    const QVariant geometry = settings.value("window/geometry");
    const QVariant state = settings.value("window/state");

    if (geometry.isValid())
    {
        restoreGeometry(geometry.toByteArray());
    }

    if (state.isValid())
    {
        restoreState(state.toByteArray());
    }
}

void MainWindow::LoadUserReplayBookmarks()
{
    m_userReplayBookmarks.clear();

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    const QString raw = settings.value("replay/userBookmarks").toString();
    if (raw.trimmed().isEmpty())
    {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
    {
        return;
    }

    const QJsonArray entries = doc.array();
    m_userReplayBookmarks.reserve(static_cast<std::size_t>(entries.size()));
    for (const QJsonValue& value : entries)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject obj = value.toObject();
        sd::transport::PlaybackMarker marker;
        marker.timestampUs = obj.value("timestampUs").toVariant().toLongLong();
        marker.kind = sd::transport::PlaybackMarkerKind::Generic;
        marker.label = obj.value("label").toString();
        if (marker.label.trimmed().isEmpty())
        {
            marker.label = QString("Bookmark %1").arg(FormatReplayTimeUs(marker.timestampUs));
        }
        m_userReplayBookmarks.push_back(marker);
    }
}

void MainWindow::PersistUserReplayBookmarks() const
{
    QJsonArray entries;
    for (const sd::transport::PlaybackMarker& marker : m_userReplayBookmarks)
    {
        QJsonObject obj;
        obj.insert("timestampUs", QJsonValue::fromVariant(QVariant::fromValue<qlonglong>(marker.timestampUs)));
        obj.insert("label", marker.label);
        entries.append(obj);
    }

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("replay/userBookmarks", QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact)));
}

void MainWindow::SaveWindowGeometry() const
{
    // Save persisted main-window geometry/state for next launch.
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    settings.setValue("replay/markersVisible", m_replayMarkersPreferredVisible);

    PersistUserReplayBookmarks();
}

void MainWindow::OnControlBoolEdited(const QString& key, bool value)
{
    if (m_transport)
    {
        m_transport->PublishBool(key, value);
    }
}

void MainWindow::OnRemoveWidgetRequested(const QString& key)
{
    const std::string keyStd = key.toStdString();
    auto it = m_tiles.find(keyStd);
    if (it == m_tiles.end())
    {
        return;
    }

    if (it->second != nullptr)
    {
        it->second->deleteLater();
    }

    m_tiles.erase(it);
    m_savedLayoutByKey.erase(keyStd);
    MarkLayoutDirty();
}

void MainWindow::OnControlDoubleEdited(const QString& key, double value)
{
    if (m_transport)
    {
        m_transport->PublishDouble(key, value);
    }
}

void MainWindow::OnControlStringEdited(const QString& key, const QString& value)
{
    if (m_transport)
    {
        m_transport->PublishString(key, value);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != nullptr && event != nullptr && !m_suppressLayoutDirty)
    {
        auto* tile = qobject_cast<sd::widgets::VariableTile*>(watched);
        if (tile != nullptr && m_isEditable)
        {
            if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
            {
                MarkLayoutDirty();
            }
            else if (event->type() == QEvent::DynamicPropertyChange)
            {
                MarkLayoutDirty();
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_layoutDirty)
    {
        event->accept();
        return;
    }

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Question);
    prompt.setWindowTitle("Save Layout");
    prompt.setText("Save changes to current layout?");
    prompt.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    prompt.setDefaultButton(QMessageBox::Yes);

    const int choice = prompt.exec();
    if (choice == QMessageBox::Cancel)
    {
        event->ignore();
        return;
    }

    if (choice == QMessageBox::Yes)
    {
        if (!SaveLayoutUsingCurrentOrPrompt())
        {
            event->ignore();
            return;
        }
    }

    event->accept();
}

bool MainWindow::SaveLayoutToPath(const QString& path)
{
    const bool saved = sd::layout::SaveLayout(m_canvas, path);
    if (saved)
    {
        m_layoutFilePath = path;
        PersistLastLayoutPath(path);
        m_layoutDirty = false;
        RefreshWindowTitle();
    }

    return saved;
}

bool MainWindow::SaveLayoutUsingCurrentOrPrompt()
{
    if (HasActiveJsonLayoutPath())
    {
        return SaveLayoutToPath(m_layoutFilePath);
    }

    QString selected = QFileDialog::getSaveFileName(
        this,
        "Save Layout",
        GetInitialLayoutPath(),
        "Layout Files (*.json);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return false;
    }

    if (QFileInfo(selected).suffix().isEmpty())
    {
        selected += ".json";
    }

    return SaveLayoutToPath(selected);
}

bool MainWindow::LoadLayoutFromPath(const QString& path, bool applyToExistingTiles, bool persistAsCurrentPath)
{
    std::vector<sd::layout::WidgetLayoutEntry> entries;
    if (!sd::layout::LoadLayoutEntries(path, entries))
    {
        return false;
    }

    m_suppressLayoutDirty = true;

    m_savedLayoutByKey.clear();
    for (const sd::layout::WidgetLayoutEntry& entry : entries)
    {
        m_savedLayoutByKey[entry.variableKey.toStdString()] = entry;
    }

    if (applyToExistingTiles)
    {
        for (const sd::layout::WidgetLayoutEntry& entry : entries)
        {
            const sd::widgets::VariableType inferredType = ToVariableTypeFromWidgetType(entry.widgetType);
            sd::widgets::VariableTile* tile = GetOrCreateTile(entry.variableKey, inferredType);
            ApplyLayoutEntryToTile(tile, entry);
        }
    }

    if (persistAsCurrentPath)
    {
        m_layoutFilePath = path;
        PersistLastLayoutPath(path);
    }
    m_layoutDirty = !persistAsCurrentPath;
    m_suppressLayoutDirty = false;
    RefreshWindowTitle();
    return true;
}

QString MainWindow::GetInitialLayoutPath() const
{
    if (!m_layoutFilePath.isEmpty())
    {
        return m_layoutFilePath;
    }

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    const QString persisted = settings.value("layout/lastPath").toString();
    if (!persisted.isEmpty())
    {
        return persisted;
    }

    return sd::layout::GetDefaultLayoutPath();
}

void MainWindow::PersistLastLayoutPath(const QString& path) const
{
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("layout/lastPath", path);
}

void MainWindow::MarkLayoutDirty()
{
    m_layoutDirty = true;
    RefreshWindowTitle();
}

bool MainWindow::HasActiveJsonLayoutPath() const
{
    if (m_layoutFilePath.isEmpty())
    {
        return false;
    }

    return QFileInfo(m_layoutFilePath).suffix().compare("json", Qt::CaseInsensitive) == 0;
}

QString MainWindow::GetLayoutTitleSegment() const
{
    if (!HasActiveJsonLayoutPath())
    {
        return "";
    }

    return QFileInfo(m_layoutFilePath).completeBaseName();
}

void MainWindow::RefreshWindowTitle()
{
    QString connectionText = "Disconnected";
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        const QString replayFile = m_connectionConfig.replayFilePath.trimmed();
        const QString replayName = replayFile.isEmpty() ? "no file selected" : QFileInfo(replayFile).fileName();
        connectionText = QString("Replay (%1)").arg(replayName);
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Connecting))
    {
        connectionText = "Connecting";
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Connected))
    {
        connectionText = "Connected";
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Stale))
    {
        connectionText = "Stale";
    }

    const QString layoutName = GetLayoutTitleSegment();
    const QString dirtySuffix = m_layoutDirty ? " *" : "";
    if (!layoutName.isEmpty())
    {
        setWindowTitle(QString("SmartDashboard - %1 [%2]%3").arg(connectionText, layoutName, dirtySuffix));
        return;
    }

    setWindowTitle(QString("SmartDashboard - %1%2").arg(connectionText, dirtySuffix));
}

void MainWindow::OnConnectTransport()
{
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        return;
    }

    StartTransport();
}

void MainWindow::OnDisconnectTransport()
{
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        return;
    }

    StopTransport();
    UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
}

void MainWindow::OnUseDirectTransport()
{
    const bool kindChanged = m_connectionConfig.kind != sd::transport::TransportKind::Direct;
    if (kindChanged && m_transport != nullptr)
    {
        StopTransport();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    m_connectionConfig.kind = sd::transport::TransportKind::Direct;
    for (const auto& [_, tile] : m_tiles)
    {
        if (tile != nullptr)
        {
            tile->SetTitleText(BuildDisplayLabel(tile->GetKey()));
        }
    }
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
    UpdateWindowConnectionText(m_connectionState);
    UpdatePlaybackUiState();
}

void MainWindow::OnUseNetworkTablesTransport()
{
    const bool kindChanged = m_connectionConfig.kind != sd::transport::TransportKind::NetworkTables;
    if (kindChanged && m_transport != nullptr)
    {
        StopTransport();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    m_connectionConfig.kind = sd::transport::TransportKind::NetworkTables;
    for (const auto& [_, tile] : m_tiles)
    {
        if (tile != nullptr)
        {
            tile->SetTitleText(BuildDisplayLabel(tile->GetKey()));
        }
    }
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
    UpdateWindowConnectionText(m_connectionState);
    UpdatePlaybackUiState();
}

void MainWindow::OnUseReplayTransport()
{
    if (!m_telemetryFeatureEnabled)
    {
        return;
    }

    const bool kindChanged = m_connectionConfig.kind != sd::transport::TransportKind::Replay;
    if (kindChanged && m_transport != nullptr)
    {
        StopTransport();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    m_connectionConfig.kind = sd::transport::TransportKind::Replay;
    for (const auto& [_, tile] : m_tiles)
    {
        if (tile != nullptr)
        {
            tile->SetTitleText(BuildDisplayLabel(tile->GetKey()));
        }
    }
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
    UpdateWindowConnectionText(m_connectionState);
    UpdatePlaybackUiState();

    if (!m_connectionConfig.replayFilePath.trimmed().isEmpty())
    {
        StartTransport();
    }
}

void MainWindow::OnToggleTelemetryFeature()
{
    bool checked = m_telemetryFeatureEnabled;
    if (sender() == m_telemetryFeatureViewAction && m_telemetryFeatureViewAction != nullptr)
    {
        checked = m_telemetryFeatureViewAction->isChecked();
    }
    else if (m_telemetryFeatureViewAction != nullptr)
    {
        checked = m_telemetryFeatureViewAction->isChecked();
    }
    m_telemetryFeatureEnabled = checked;

    if (m_telemetryFeatureViewAction != nullptr)
    {
        const bool prior = m_telemetryFeatureViewAction->blockSignals(true);
        m_telemetryFeatureViewAction->setChecked(m_telemetryFeatureEnabled);
        m_telemetryFeatureViewAction->blockSignals(prior);
    }

    const bool hadTransport = (m_transport != nullptr);
    if (!m_telemetryFeatureEnabled)
    {
        StopSessionRecording();
        if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
        {
            m_connectionConfig.kind = sd::transport::TransportKind::Direct;
            for (const auto& [_, tile] : m_tiles)
            {
                if (tile != nullptr)
                {
                    tile->SetTitleText(BuildDisplayLabel(tile->GetKey()));
                }
            }

            if (hadTransport)
            {
                StartTransport();
            }
        }
    }

    PersistConnectionSettings();
    ApplyTransportMenuChecks();
    UpdatePlaybackUiState();
}

void MainWindow::OnSetNtHost()
{
    bool ok = false;
    const QString value = QInputDialog::getText(
        this,
        "NetworkTables Host",
        "Enter NT host/IP:",
        QLineEdit::Normal,
        m_connectionConfig.ntHost,
        &ok
    );

    if (!ok || value.trimmed().isEmpty())
    {
        return;
    }

    m_connectionConfig.ntHost = value.trimmed();
    m_connectionConfig.ntUseTeam = false;
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
}

void MainWindow::OnSetNtTeam()
{
    bool ok = false;
    const int team = QInputDialog::getInt(
        this,
        "NetworkTables Team",
        "Enter team number:",
        m_connectionConfig.ntTeam,
        0,
        99999,
        1,
        &ok
    );

    if (!ok)
    {
        return;
    }

    m_connectionConfig.ntTeam = team;
    m_connectionConfig.ntUseTeam = true;
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
}

void MainWindow::OnToggleNtUseTeam()
{
    m_connectionConfig.ntUseTeam = m_ntUseTeamAction != nullptr && m_ntUseTeamAction->isChecked();
    PersistConnectionSettings();
}

void MainWindow::OnOpenReplayFile()
{
    const QString selected = QFileDialog::getOpenFileName(
        this,
        "Open Replay Session",
        m_connectionConfig.replayFilePath,
        "Replay Files (*.json);;All Files (*)"
    );
    if (selected.isEmpty())
    {
        return;
    }

    m_connectionConfig.replayFilePath = selected;
    m_connectionConfig.kind = sd::transport::TransportKind::Replay;
    ApplyTransportMenuChecks();
    PersistConnectionSettings();
    UpdateWindowConnectionText(m_connectionState);
    StartTransport();
}

void MainWindow::OnRecordToggled(bool checked)
{
    m_recordRequested = checked;
    PersistConnectionSettings();

    if (checked)
    {
        StartSessionRecording();
    }
    else
    {
        StopSessionRecording();
    }

    UpdatePlaybackUiState();
}

void MainWindow::OnPlaybackRewindToStart()
{
    if (!m_transport || !m_transport->SupportsPlayback() || !m_telemetryFeatureEnabled)
    {
        return;
    }

    m_transport->SetPlaybackPlaying(false);
    m_transport->SeekPlaybackUs(0);
    UpdatePlaybackUiState();
}

void MainWindow::OnPlaybackPlayPause()
{
    if (!m_transport || !m_transport->SupportsPlayback() || !m_telemetryFeatureEnabled)
    {
        return;
    }

    const bool isPlaying = m_transport->IsPlaybackPlaying();
    m_transport->SetPlaybackPlaying(!isPlaying);
    UpdatePlaybackUiState();
}

void MainWindow::OnPlaybackRateChanged(int index)
{
    if (!m_transport || !m_transport->SupportsPlayback() || m_playbackRateCombo == nullptr)
    {
        return;
    }

    const double rate = m_playbackRateCombo->itemData(index).toDouble();
    m_transport->SetPlaybackRate(rate);
}

void MainWindow::OnPlaybackCursorScrubbed(std::int64_t cursorUs)
{
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    m_transport->SeekPlaybackUs(cursorUs);
    UpdatePlaybackUiState();
}

void MainWindow::OnPlaybackPreviousMarker()
{
    if (!m_transport || !m_transport->SupportsPlayback() || m_replayMarkerTimesUs.empty())
    {
        return;
    }

    const std::int64_t cursorUs = m_transport->GetPlaybackCursorUs();
    std::int64_t targetUs = -1;
    for (std::size_t i = m_replayMarkerTimesUs.size(); i > 0; --i)
    {
        const std::int64_t markerUs = m_replayMarkerTimesUs[i - 1];
        if (markerUs < cursorUs)
        {
            targetUs = markerUs;
            break;
        }
    }

    if (targetUs < 0)
    {
        targetUs = m_replayMarkerTimesUs.front();
    }

    m_transport->SeekPlaybackUs(targetUs);
    UpdatePlaybackUiState();
}

void MainWindow::OnPlaybackNextMarker()
{
    if (!m_transport || !m_transport->SupportsPlayback() || m_replayMarkerTimesUs.empty())
    {
        return;
    }

    const std::int64_t cursorUs = m_transport->GetPlaybackCursorUs();
    std::int64_t targetUs = -1;
    for (const std::int64_t markerUs : m_replayMarkerTimesUs)
    {
        if (markerUs > cursorUs)
        {
            targetUs = markerUs;
            break;
        }
    }

    if (targetUs < 0)
    {
        targetUs = m_replayMarkerTimesUs.back();
    }

    m_transport->SeekPlaybackUs(targetUs);
    UpdatePlaybackUiState();
}

void MainWindow::OnReplayMarkerActivated(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    const std::int64_t markerUs = item->data(Qt::UserRole).toLongLong();
    m_transport->SeekPlaybackUs(markerUs);
    UpdatePlaybackUiState();
}

void MainWindow::OnAddReplayBookmark()
{
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    const std::int64_t cursorUs = m_transport->GetPlaybackCursorUs();
    constexpr std::int64_t dedupeThresholdUs = 500000;
    for (const sd::transport::PlaybackMarker& marker : m_userReplayBookmarks)
    {
        if (std::llabs(marker.timestampUs - cursorUs) <= dedupeThresholdUs)
        {
            return;
        }
    }

    sd::transport::PlaybackMarker bookmark;
    bookmark.timestampUs = cursorUs;
    bookmark.kind = sd::transport::PlaybackMarkerKind::Generic;
    bookmark.label = QString("Bookmark %1").arg(FormatReplayTimeUs(cursorUs));
    m_userReplayBookmarks.push_back(bookmark);
    PersistUserReplayBookmarks();
    UpdatePlaybackUiState();
}

void MainWindow::OnClearReplayBookmarks()
{
    if (m_userReplayBookmarks.empty())
    {
        return;
    }

    m_userReplayBookmarks.clear();
    PersistUserReplayBookmarks();
    UpdatePlaybackUiState();
}

void MainWindow::ApplyTransportMenuChecks()
{
    const bool replayMode = m_connectionConfig.kind == sd::transport::TransportKind::Replay;

    if (m_useDirectTransportAction != nullptr)
    {
        m_useDirectTransportAction->setChecked(m_connectionConfig.kind == sd::transport::TransportKind::Direct);
    }

    if (m_useNetworkTablesTransportAction != nullptr)
    {
        m_useNetworkTablesTransportAction->setChecked(m_connectionConfig.kind == sd::transport::TransportKind::NetworkTables);
    }

    if (m_useReplayTransportAction != nullptr)
    {
        m_useReplayTransportAction->setChecked(m_connectionConfig.kind == sd::transport::TransportKind::Replay);
    }

    if (m_ntUseTeamAction != nullptr)
    {
        m_ntUseTeamAction->setChecked(m_connectionConfig.ntUseTeam);
    }

    if (m_useReplayTransportAction != nullptr)
    {
        m_useReplayTransportAction->setEnabled(m_telemetryFeatureEnabled);
    }

    if (m_openReplayFileAction != nullptr)
    {
        m_openReplayFileAction->setEnabled(m_telemetryFeatureEnabled);
    }

    if (m_connectTransportAction != nullptr)
    {
        m_connectTransportAction->setEnabled(!replayMode);
    }

    if (m_disconnectTransportAction != nullptr)
    {
        m_disconnectTransportAction->setEnabled(!replayMode);
    }
}

void MainWindow::PersistConnectionSettings() const
{
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("connection/transportKind", static_cast<int>(m_connectionConfig.kind));
    settings.setValue("connection/ntHost", m_connectionConfig.ntHost);
    settings.setValue("connection/ntTeam", m_connectionConfig.ntTeam);
    settings.setValue("connection/ntUseTeam", m_connectionConfig.ntUseTeam);
    settings.setValue("connection/ntClientName", m_connectionConfig.ntClientName);
    settings.setValue("connection/replayFilePath", m_connectionConfig.replayFilePath);
    settings.setValue("telemetry/enabled", m_telemetryFeatureEnabled);
    settings.setValue("telemetry/recordEnabled", m_recordRequested);
}

void MainWindow::StartTransport()
{
    StopTransport();
    StartSessionRecording();

    m_transport = sd::transport::CreateDashboardTransport(m_connectionConfig);
    if (!m_transport)
    {
        StopSessionRecording();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
        return;
    }

    const bool started = m_transport->Start(
        [this](const sd::transport::VariableUpdate& update)
        {
            QMetaObject::invokeMethod(this, [this, update]()
            {
                OnVariableUpdateReceived(update.key, update.valueType, update.value, static_cast<quint64>(update.seq));
            }, Qt::QueuedConnection);
        },
        [this](sd::transport::ConnectionState state)
        {
            QMetaObject::invokeMethod(this, [this, state]()
            {
                OnConnectionStateChanged(static_cast<int>(state));
            }, Qt::QueuedConnection);
        }
    );

    if (started)
    {
        for (const auto& [_, tile] : m_tiles)
        {
            if (tile != nullptr)
            {
                tile->SetTitleText(BuildDisplayLabel(tile->GetKey()));
            }
        }

        if (m_transport->SupportsPlayback() && m_playbackRateCombo != nullptr)
        {
            m_transport->SetPlaybackRate(m_playbackRateCombo->currentData().toDouble());
        }
    }

    if (!started)
    {
        StopSessionRecording();
        m_transport.reset();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    UpdatePlaybackUiState();
}

void MainWindow::StopTransport()
{
    if (m_transport)
    {
        RecordConnectionEvent(static_cast<int>(sd::transport::ConnectionState::Disconnected));
        m_transport->Stop();
        m_transport.reset();
    }

    StopSessionRecording();
    UpdatePlaybackUiState();
}

void MainWindow::UpdatePlaybackUiState()
{
    const bool replayMode = m_connectionConfig.kind == sd::transport::TransportKind::Replay;
    const bool hasPlayback = replayMode && (m_transport != nullptr) && m_transport->SupportsPlayback() && m_telemetryFeatureEnabled;
    const bool canRecordOnTransport = m_telemetryFeatureEnabled && IsRecordingTransportKind(m_connectionConfig.kind);

    if (m_playbackRateCombo != nullptr)
    {
        m_playbackRateCombo->setEnabled(hasPlayback);
    }

    if (m_recordButton != nullptr)
    {
        m_recordButton->setEnabled(canRecordOnTransport);
        if (m_recordButton->isChecked() != m_recordRequested)
        {
            m_recordButton->setChecked(m_recordRequested);
        }
    }

    if (m_playPauseButton != nullptr)
    {
        m_playPauseButton->setEnabled(hasPlayback);
        if (hasPlayback && m_transport != nullptr && m_transport->IsPlaybackPlaying())
        {
            m_playPauseButton->setText(QString::fromUtf8("\xE2\x8F\xB8"));
            m_playPauseButton->setStyleSheet(
                "QToolButton { color: #f2c94c; font-weight: 700; }"
                "QToolButton:disabled { color: #5a5a5a; }"
            );
            m_playPauseButton->setToolTip("Pause telemetry replay");
        }
        else
        {
            m_playPauseButton->setText(QString::fromUtf8("\xE2\x96\xB6"));
            m_playPauseButton->setStyleSheet(
                "QToolButton { color: #2f9e44; font-weight: 700; }"
                "QToolButton:disabled { color: #5a5a5a; }"
            );
            m_playPauseButton->setToolTip("Play telemetry replay");
        }
    }

    if (m_rewindButton != nullptr)
    {
        m_rewindButton->setEnabled(hasPlayback);
    }

    if (m_addBookmarkButton != nullptr)
    {
        m_addBookmarkButton->setEnabled(hasPlayback);
    }

    if (m_clearBookmarksButton != nullptr)
    {
        m_clearBookmarksButton->setEnabled(hasPlayback && !m_userReplayBookmarks.empty());
    }

    if (hasPlayback)
    {
        RefreshReplayMarkers();
    }
    else
    {
        m_replayMarkerTimesUs.clear();
    }

    const bool hasMarkers = hasPlayback && !m_replayMarkerTimesUs.empty();
    if (m_prevMarkerButton != nullptr)
    {
        m_prevMarkerButton->setEnabled(hasMarkers);
    }
    if (m_nextMarkerButton != nullptr)
    {
        m_nextMarkerButton->setEnabled(hasMarkers);
    }

    const bool dockContextVisible = m_telemetryFeatureEnabled && replayMode;

    const bool allowMarkerDockAction = dockContextVisible;
    if (m_replayMarkersViewAction != nullptr)
    {
        m_replayMarkersViewAction->setEnabled(allowMarkerDockAction);
    }

    if (m_replayControlsViewAction != nullptr)
    {
        m_replayControlsViewAction->setEnabled(dockContextVisible);
    }

    if (m_replayTimelineViewAction != nullptr)
    {
        m_replayTimelineViewAction->setEnabled(dockContextVisible);
    }

    if (m_replayControlsDock != nullptr)
    {
        m_syncingReplayControlsDockVisibility = true;
        m_replayControlsDock->setEnabled(dockContextVisible);
        if (!dockContextVisible)
        {
            m_replayControlsDock->hide();
        }
        else if (m_replayControlsPreferredVisible)
        {
            m_replayControlsDock->show();
        }
        else
        {
            m_replayControlsDock->hide();
        }
        m_syncingReplayControlsDockVisibility = false;
        if (m_replayControlsViewAction != nullptr)
        {
            const bool prior = m_replayControlsViewAction->blockSignals(true);
            m_replayControlsViewAction->setChecked(m_replayControlsPreferredVisible);
            m_replayControlsViewAction->blockSignals(prior);
        }
    }

    if (m_replayTimelineDock != nullptr)
    {
        m_syncingReplayTimelineDockVisibility = true;
        m_replayTimelineDock->setEnabled(dockContextVisible);
        if (!dockContextVisible)
        {
            m_replayTimelineDock->hide();
        }
        else if (m_replayTimelinePreferredVisible)
        {
            m_replayTimelineDock->show();
        }
        else
        {
            m_replayTimelineDock->hide();
        }
        m_syncingReplayTimelineDockVisibility = false;
        if (m_replayTimelineViewAction != nullptr)
        {
            const bool prior = m_replayTimelineViewAction->blockSignals(true);
            m_replayTimelineViewAction->setChecked(m_replayTimelinePreferredVisible);
            m_replayTimelineViewAction->blockSignals(prior);
        }
    }

    if (m_replayMarkerDock != nullptr)
    {
        const bool showDockByContext = dockContextVisible;
        const bool preferVisible = m_replayMarkersPreferredVisible;
        m_syncingReplayMarkerDockVisibility = true;
        m_replayMarkerDock->setEnabled(showDockByContext);
        if (!showDockByContext)
        {
            m_replayMarkerDock->hide();
        }
        else if (preferVisible)
        {
            m_replayMarkerDock->show();
        }
        else
        {
            m_replayMarkerDock->hide();
        }
        if (m_replayMarkersViewAction != nullptr)
        {
            const bool prior = m_replayMarkersViewAction->blockSignals(true);
            m_replayMarkersViewAction->setChecked(m_replayMarkersPreferredVisible);
            m_replayMarkersViewAction->blockSignals(prior);
        }
        m_syncingReplayMarkerDockVisibility = false;
    }

    if (m_playbackTimeline != nullptr)
    {
        m_playbackTimeline->setEnabled(hasPlayback);
        m_playbackTimeline->setWindowOpacity(hasPlayback ? 1.0 : 0.45);
        if (hasPlayback)
        {
            const std::int64_t durationUs = m_transport->GetPlaybackDurationUs();
            const std::int64_t cursorUs = m_transport->GetPlaybackCursorUs();
            m_playbackTimeline->SetDurationUs(durationUs);
            if (m_playbackTimeline->GetWindowEndUs() <= m_playbackTimeline->GetWindowStartUs())
            {
                m_playbackTimeline->SetWindowUs(0, durationUs);
            }
            m_playbackTimeline->SetCursorUs(cursorUs);

            const std::int64_t windowStartUs = m_playbackTimeline->GetWindowStartUs();
            const std::int64_t windowEndUs = m_playbackTimeline->GetWindowEndUs();
            const std::int64_t spanUs = std::max<std::int64_t>(1, windowEndUs - windowStartUs);
            const std::int64_t thresholdUs = windowStartUs + static_cast<std::int64_t>(0.85 * static_cast<double>(spanUs));
            if (cursorUs > thresholdUs)
            {
                std::int64_t newStartUs = cursorUs - static_cast<std::int64_t>(0.85 * static_cast<double>(spanUs));
                std::int64_t newEndUs = newStartUs + spanUs;
                if (durationUs > 0 && newEndUs > durationUs)
                {
                    newEndUs = durationUs;
                    newStartUs = std::max<std::int64_t>(0, newEndUs - spanUs);
                }
                m_playbackTimeline->SetWindowUs(newStartUs, newEndUs);
            }

            std::vector<sd::widgets::TimelineMarker> timelineMarkers;
            timelineMarkers.reserve(m_replayMarkers.size());
            for (const sd::transport::PlaybackMarker& marker : m_replayMarkers)
            {
                sd::widgets::TimelineMarker timelineMarker;
                timelineMarker.timestampUs = marker.timestampUs;
                switch (marker.kind)
                {
                    case sd::transport::PlaybackMarkerKind::Connect:
                        timelineMarker.kind = sd::widgets::TimelineMarkerKind::Connect;
                        break;
                    case sd::transport::PlaybackMarkerKind::Disconnect:
                        timelineMarker.kind = sd::widgets::TimelineMarkerKind::Disconnect;
                        break;
                    case sd::transport::PlaybackMarkerKind::Stale:
                        timelineMarker.kind = sd::widgets::TimelineMarkerKind::Stale;
                        break;
                    case sd::transport::PlaybackMarkerKind::Anomaly:
                        timelineMarker.kind = sd::widgets::TimelineMarkerKind::Anomaly;
                        break;
                    case sd::transport::PlaybackMarkerKind::Generic:
                    default:
                        timelineMarker.kind = sd::widgets::TimelineMarkerKind::Generic;
                        break;
                }
                timelineMarker.label = marker.label;
                timelineMarkers.push_back(timelineMarker);
            }
            m_playbackTimeline->SetMarkers(timelineMarkers);
            RefreshReplayMarkerList(cursorUs);
        }
        else
        {
            m_playbackTimeline->SetDurationUs(0);
            m_playbackTimeline->SetCursorUs(0);
            m_playbackTimeline->SetWindowUs(0, 0);
            m_playbackTimeline->SetMarkers({});
            RefreshReplayMarkerList(0);
        }
    }
}

void MainWindow::RefreshReplayMarkers()
{
    m_replayMarkers.clear();
    m_replayMarkerTimesUs.clear();
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    m_replayMarkers = m_transport->GetPlaybackMarkers();
    for (const sd::transport::PlaybackMarker& marker : m_userReplayBookmarks)
    {
        m_replayMarkers.push_back(marker);
    }
    std::sort(
        m_replayMarkers.begin(),
        m_replayMarkers.end(),
        [](const sd::transport::PlaybackMarker& lhs, const sd::transport::PlaybackMarker& rhs)
        {
            return lhs.timestampUs < rhs.timestampUs;
        }
    );

    m_replayMarkerTimesUs.reserve(m_replayMarkers.size());
    for (const sd::transport::PlaybackMarker& marker : m_replayMarkers)
    {
        m_replayMarkerTimesUs.push_back(marker.timestampUs);
    }
}

void MainWindow::RefreshReplayMarkerList(std::int64_t cursorUs)
{
    if (m_replayMarkerList == nullptr)
    {
        return;
    }

    bool rebuildNeeded = (m_replayMarkerList->count() != static_cast<int>(m_replayMarkers.size()));
    if (!rebuildNeeded)
    {
        for (int i = 0; i < m_replayMarkerList->count(); ++i)
        {
            const QListWidgetItem* item = m_replayMarkerList->item(i);
            const sd::transport::PlaybackMarker& marker = m_replayMarkers[static_cast<std::size_t>(i)];
            const QString labelText = marker.label.trimmed().isEmpty() ? MarkerKindLabel(marker.kind) : marker.label.trimmed();
            const QString itemText = QString("%1  [%2]  %3").arg(FormatReplayTimeUs(marker.timestampUs), MarkerKindLabel(marker.kind), labelText);
            if (item == nullptr || item->data(Qt::UserRole).toLongLong() != marker.timestampUs || item->text() != itemText)
            {
                rebuildNeeded = true;
                break;
            }
        }
    }

    m_syncingMarkerSelection = true;
    if (rebuildNeeded)
    {
        m_replayMarkerList->setUpdatesEnabled(false);
        while (m_replayMarkerList->count() > 0)
        {
            delete m_replayMarkerList->takeItem(0);
        }

        for (const sd::transport::PlaybackMarker& marker : m_replayMarkers)
        {
            const QString labelText = marker.label.trimmed().isEmpty() ? MarkerKindLabel(marker.kind) : marker.label.trimmed();
            const QString itemText = QString("%1  [%2]  %3").arg(FormatReplayTimeUs(marker.timestampUs), MarkerKindLabel(marker.kind), labelText);
            auto* item = new QListWidgetItem(itemText, m_replayMarkerList);
            item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(marker.timestampUs));
            item->setFlags((item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable) & ~Qt::ItemIsEditable);
            item->setToolTip("Jump replay cursor to this marker");
        }
        m_replayMarkerList->setUpdatesEnabled(true);
    }

    int selectedRow = -1;
    for (std::size_t i = 0; i < m_replayMarkers.size(); ++i)
    {
        const sd::transport::PlaybackMarker& marker = m_replayMarkers[i];
        if (marker.timestampUs <= cursorUs)
        {
            selectedRow = static_cast<int>(i);
        }
    }

    if (selectedRow < 0 && m_replayMarkerList->count() > 0)
    {
        selectedRow = 0;
    }
    if (selectedRow >= 0)
    {
        if (m_replayMarkerList->currentRow() != selectedRow)
        {
            m_replayMarkerList->setCurrentRow(selectedRow);
        }
    }

    m_replayMarkerList->setEnabled(!m_replayMarkers.empty());

    RefreshReplaySummaryLabel();

    m_syncingMarkerSelection = false;
}

void MainWindow::RefreshReplaySummaryLabel()
{
    if (m_replaySelectionSummaryLabel == nullptr)
    {
        return;
    }

    std::int64_t windowStartUs = 0;
    std::int64_t windowEndUs = 0;
    if (m_playbackTimeline != nullptr)
    {
        windowStartUs = m_playbackTimeline->GetWindowStartUs();
        windowEndUs = m_playbackTimeline->GetWindowEndUs();
    }

    int markerCount = 0;
    int anomalyCount = 0;
    int bookmarkCount = 0;
    for (const sd::transport::PlaybackMarker& marker : m_replayMarkers)
    {
        if (marker.timestampUs >= windowStartUs && marker.timestampUs <= windowEndUs)
        {
            ++markerCount;
            if (marker.kind == sd::transport::PlaybackMarkerKind::Anomaly)
            {
                ++anomalyCount;
            }
            const QString labelLower = marker.label.toLower();
            if (labelLower.startsWith("bookmark "))
            {
                ++bookmarkCount;
            }
        }
    }

    const std::int64_t spanUs = std::max<std::int64_t>(0, windowEndUs - windowStartUs);
    m_replaySelectionSummaryLabel->setText(
        QString("Window: %1 markers (%2 anomalies, %3 bookmarks), span=%4")
            .arg(markerCount)
            .arg(anomalyCount)
            .arg(bookmarkCount)
            .arg(FormatReplaySpanUs(spanUs))
    );
}

void MainWindow::StepPlaybackByUs(std::int64_t deltaUs)
{
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    const std::int64_t cursorUs = m_transport->GetPlaybackCursorUs();
    const std::int64_t targetUs = std::max<std::int64_t>(0, cursorUs + deltaUs);
    m_transport->SeekPlaybackUs(targetUs);
    UpdatePlaybackUiState();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr)
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    const bool hasPlayback =
        m_telemetryFeatureEnabled
        && m_transport != nullptr
        && m_transport->SupportsPlayback()
        && m_connectionConfig.kind == sd::transport::TransportKind::Replay;

    if (!hasPlayback)
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const std::int64_t stepUs = shift ? 1000000 : 100000;

    if (event->key() == Qt::Key_Left)
    {
        StepPlaybackByUs(-stepUs);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right)
    {
        StepPlaybackByUs(stepUs);
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::StartSessionRecording()
{
    StopSessionRecording();

    if (!m_telemetryFeatureEnabled || !m_recordRequested || !IsRecordingTransportKind(m_connectionConfig.kind))
    {
        return;
    }

    const QString logsDir = QDir::currentPath() + "/logs";
    QDir().mkpath(logsDir);

    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    m_recordingFilePath = QString("%1/session_%2.json").arg(logsDir, timestamp);

    m_recordingStartEpochUs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    m_recordingStartSteadyUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    m_recordingLastTimestampUs = 0;

    {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        m_recordingQueue.clear();
        m_recordingStopRequested = false;
        m_recordingThreadRunning = true;
    }

    m_recordingThread = std::thread([this]()
    {
        QFile file(m_recordingFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            std::lock_guard<std::mutex> lock(m_recordingMutex);
            m_recordingThreadRunning = false;
            return;
        }

        while (true)
        {
            QByteArray line;
            {
                std::unique_lock<std::mutex> lock(m_recordingMutex);
                m_recordingCv.wait(lock, [this]()
                {
                    return m_recordingStopRequested || !m_recordingQueue.empty();
                });

                if (!m_recordingQueue.empty())
                {
                    line = std::move(m_recordingQueue.front());
                    m_recordingQueue.pop_front();
                }
                else if (m_recordingStopRequested)
                {
                    break;
                }
            }

            if (!line.isEmpty())
            {
                file.write(line);
                file.write("\n");
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_recordingMutex);
            while (!m_recordingQueue.empty())
            {
                const QByteArray line = std::move(m_recordingQueue.front());
                m_recordingQueue.pop_front();
                if (!line.isEmpty())
                {
                    file.write(line);
                    file.write("\n");
                }
            }
            m_recordingThreadRunning = false;
        }

        file.flush();
        file.close();
    });
}

void MainWindow::StopSessionRecording()
{
    {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        if (!m_recordingThreadRunning && !m_recordingThread.joinable())
        {
            return;
        }

        m_recordingStopRequested = true;
    }

    m_recordingCv.notify_all();
    if (m_recordingThread.joinable())
    {
        m_recordingThread.join();
    }

    std::lock_guard<std::mutex> lock(m_recordingMutex);
    m_recordingQueue.clear();
    m_recordingThreadRunning = false;
    m_recordingStopRequested = false;
}

void MainWindow::RecordVariableEvent(const QString& key, int valueType, const QVariant& value, quint64 seq)
{
    if (!IsRecordingTransportKind(m_connectionConfig.kind))
    {
        return;
    }

    std::uint64_t nowSteadyUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );

    std::uint64_t timestampUs = 0;
    if (nowSteadyUs >= m_recordingStartSteadyUs)
    {
        timestampUs = nowSteadyUs - m_recordingStartSteadyUs;
    }
    if (timestampUs < m_recordingLastTimestampUs)
    {
        timestampUs = m_recordingLastTimestampUs;
    }
    m_recordingLastTimestampUs = timestampUs;

    QString typeString = "string";
    if (valueType == static_cast<int>(sd::direct::ValueType::Bool))
    {
        typeString = "bool";
    }
    else if (valueType == static_cast<int>(sd::direct::ValueType::Double))
    {
        typeString = "double";
    }

    QJsonObject object;
    object.insert("eventKind", "data");
    object.insert("timestampUs", static_cast<qint64>(timestampUs));
    object.insert("key", key);
    object.insert("valueType", typeString);
    object.insert("seq", QString::number(seq));
    if (typeString == "bool")
    {
        object.insert("value", value.toBool());
    }
    else if (typeString == "double")
    {
        object.insert("value", value.toDouble());
    }
    else
    {
        object.insert("value", value.toString());
    }

    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        if (!m_recordingThreadRunning || m_recordingStopRequested)
        {
            return;
        }

        m_recordingQueue.push_back(line);
    }
    m_recordingCv.notify_one();
}

void MainWindow::RecordConnectionEvent(int state)
{
    if (!IsRecordingTransportKind(m_connectionConfig.kind))
    {
        return;
    }

    std::uint64_t nowSteadyUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );

    std::uint64_t timestampUs = 0;
    if (nowSteadyUs >= m_recordingStartSteadyUs)
    {
        timestampUs = nowSteadyUs - m_recordingStartSteadyUs;
    }
    if (timestampUs < m_recordingLastTimestampUs)
    {
        timestampUs = m_recordingLastTimestampUs;
    }
    m_recordingLastTimestampUs = timestampUs;

    QString stateText = "Disconnected";
    if (state == static_cast<int>(sd::transport::ConnectionState::Connecting))
    {
        stateText = "Connecting";
    }
    else if (state == static_cast<int>(sd::transport::ConnectionState::Connected))
    {
        stateText = "Connected";
    }
    else if (state == static_cast<int>(sd::transport::ConnectionState::Stale))
    {
        stateText = "Stale";
    }

    QJsonObject object;
    object.insert("eventKind", "connection_state");
    object.insert("timestampUs", static_cast<qint64>(timestampUs));
    object.insert("state", stateText);
    object.insert("stateValue", state);

    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        if (!m_recordingThreadRunning || m_recordingStopRequested)
        {
            return;
        }

        m_recordingQueue.push_back(line);
    }
    m_recordingCv.notify_one();
}

bool MainWindow::IsRecordingTransportKind(sd::transport::TransportKind kind) const
{
    return kind == sd::transport::TransportKind::Direct || kind == sd::transport::TransportKind::NetworkTables;
}
