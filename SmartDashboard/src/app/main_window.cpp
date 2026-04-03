#include "app/main_window.h"

#include "app/debug_log_paths.h"

#include "layout/layout_serializer.h"
#include "sd_direct_types.h"
#include "widgets/playback_timeline_widget.h"
#include "widgets/line_plot_widget.h"
#include "widgets/run_browser_dock.h"
#include "widgets/camera_viewer_dock.h"
#include "camera/camera_discovery_aggregator.h"
#include "camera/camera_publisher_discovery.h"
#include "camera/static_camera_source.h"

#include <QAction>
#include <QActionGroup>
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPalette>
#include <QCoreApplication>
#include <QPushButton>
#include <QRubberBand>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QStatusBar>
#include <QProcessEnvironment>
#include <QThread>
#include <QToolButton>
#include <QTimer>
#include <QVariant>
#include <QCheckBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWidget>

#include <QtWidgets/QDockWidget>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtCore/QJsonArray>
#include <QtCore/QPoint>

#include <algorithm>
#include <chrono>

#ifdef _DEBUG
#include <QLocalServer>
#include <QLocalSocket>
#endif
#include <fstream>
#include <limits>

namespace
{
    // Ian: This allowlist is intentional and narrow by design.  It gates the
    // DebugLogUiEvent path in OnVariableUpdate so only harness-relevant keys
    // write to the headless UI log that automated scripts parse.  ALL delivered
    // keys still create tiles and receive live updates — this function only
    // controls which keys are noise-filtered out of the debug log.
    //
    // Ian: Heading and Travel_Heading ARE delivered (tiles are created, sequence
    // gap analysis confirms receipt) but are NOT in this list because they are
    // not currently used by the automated stress/verification scripts.  If you
    // add a new key to the harness scripts, add it here too — otherwise the
    // scripts will see zero log lines for it even though the transport is fine.
    bool IsHarnessFocusKey(const QString& key)
    {
        return key == "Test/AutonTest"
            || key == "TestMove"
            || key == "Timer"
            || key == "Y_ft"
            || key == "X_ft"
            || key == "Velocity"
            || key == "Rotation Velocity"
            || key == "Wheel_fl_Velocity"
            || key == "Wheel_fr_Velocity"
            || key == "Wheel_rl_Velocity"
            || key == "Wheel_rr_Velocity"
            || key == "Test/Auton_Selection/AutoChooser/selected";
    }

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

    void SetTimelineDockMode(sd::widgets::PlaybackTimelineWidget* timeline, bool floating)
    {
        if (timeline == nullptr)
        {
            return;
        }

        if (floating)
        {
            timeline->setMinimumHeight(58);
        }
        else
        {
            timeline->setMinimumHeight(42);
        }
    }

    sd::widgets::VariableType ToVariableType(int valueType)
    {
        switch (valueType)
        {
            case static_cast<int>(sd::direct::ValueType::Bool):
                return sd::widgets::VariableType::Bool;
            case static_cast<int>(sd::direct::ValueType::Double):
                return sd::widgets::VariableType::Double;
            case static_cast<int>(sd::direct::ValueType::StringArray):
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

    bool IsOperatorControlWidget(const sd::widgets::VariableTile* tile)
    {
        if (tile == nullptr)
        {
            return false;
        }

        const QString widgetType = tile->GetWidgetType();
        return (widgetType == "bool.checkbox") ||
               (widgetType == "double.numeric") ||
               (widgetType == "double.slider") ||
               (widgetType == "string.edit") ||
               (widgetType == "string.chooser");
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

        if (entry.showLabel.isValid())
        {
            tile->SetShowLabel(entry.showLabel.toBool());
        }

        if (entry.stringChooserMode.isValid())
        {
            tile->SetStringChooserMode(entry.stringChooserMode.toBool());
        }

        if (entry.stringChooserOptions.isValid())
        {
            tile->SetStringChooserOptions(entry.stringChooserOptions.toStringList());
        }

        // Ian: Restore multi-line plot sources from saved layout.  Converts
        // LinePlotSourceEntry (key + hex color string + originalWidgetType)
        // to PlotSourceEntry (key + QColor + originalWidgetType) and pushes
        // them into the tile.  SetPlotSources rebuilds the series on the
        // LinePlotWidget.  MainWindow is responsible for wiring the reverse
        // map + hiding absorbed tiles after this function returns.
        if (!entry.linePlotSources.empty())
        {
            std::vector<sd::widgets::PlotSourceEntry> sources;
            sources.reserve(entry.linePlotSources.size());
            for (const auto& src : entry.linePlotSources)
            {
                sd::widgets::PlotSourceEntry pe;
                pe.key = src.key;
                pe.color = QColor(src.color);
                if (!pe.color.isValid())
                {
                    pe.color = QColor("#e74c3c");  // fallback red
                }
                pe.originalWidgetType = src.originalWidgetType;
                pe.visible = src.visible;
                sources.push_back(pe);
            }
            tile->SetPlotSources(sources);
        }

        // Ian: Restore multi-line drop target mode from saved layout.
        if (entry.multiLinePlotMode)
        {
            tile->SetMultiLinePlotMode(true);
        }

        // Ian: Re-apply saved geometry after all property setters have run.
        // Several setters (show-label, progress bar percentage) call
        // UpdateWidgetPresentation() which can resize the tile to enforce
        // minimum/compact widths.  If the property application order means an
        // intermediate state triggers a resize, the originally-loaded geometry
        // gets overwritten.  This final setGeometry ensures the persisted
        // layout always wins.
        tile->setGeometry(entry.geometry);

    }

    bool IsTemporaryDefaultEligibleWidget(const sd::widgets::VariableTile* tile)
    {
        if (tile == nullptr)
        {
            return false;
        }

        const QString widgetType = tile->GetWidgetType();
        return widgetType == "bool.checkbox"
            || widgetType == "bool.led"
            || widgetType == "bool.text"
            || widgetType == "double.numeric"
            || widgetType == "double.progress"
            || widgetType == "double.gauge"
            || widgetType == "double.slider"
            || widgetType == "string.text"
            || widgetType == "string.multiline"
            || widgetType == "string.edit"
            || widgetType == "string.chooser";
    }

    QVariant TemporaryDefaultValueForTile(const sd::widgets::VariableTile* tile)
    {
        if (tile == nullptr)
        {
            return {};
        }

        const QString widgetType = tile->GetWidgetType();
        if (widgetType == "bool.checkbox")
        {
            return QVariant(false);
        }

        if (widgetType == "bool.led" || widgetType == "bool.text")
        {
            return QVariant(false);
        }

        if (widgetType == "double.numeric" || widgetType == "double.progress" || widgetType == "double.gauge")
        {
            return QVariant(0.0);
        }

        if (widgetType == "double.slider")
        {
            return QVariant(0.0);
        }

        if (widgetType == "string.text" || widgetType == "string.multiline" || widgetType == "string.edit")
        {
            return QVariant(QString());
        }

        if (widgetType == "string.chooser")
        {
            const QStringList options = tile->GetStringChooserOptions();
            if (!options.isEmpty())
            {
                return QVariant(options.front());
            }
        }

        return {};
    }
}

MainWindow::MainWindow(QWidget* parent, bool startTransportOnInit)
    : QMainWindow(parent)
{
    RefreshWindowTitle();
    resize(1200, 800);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName("dashboardCanvas");
    m_canvas->setAutoFillBackground(true);
    m_canvas->setBackgroundRole(QPalette::Window);
    m_canvas->installEventFilter(this);
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

    m_runBrowserViewAction = viewMenu->addAction("Run Browser");
    m_runBrowserViewAction->setCheckable(true);
    m_runBrowserViewAction->setChecked(false);

    m_cameraViewAction = viewMenu->addAction("Camera");
    m_cameraViewAction->setCheckable(true);
    m_cameraViewAction->setChecked(false);

    viewMenu->addSeparator();
    m_resetAllLinePlotsAction = viewMenu->addAction("Reset All Line Plots");
    m_resetAllLinePlotsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    m_resetAllLinePlotsAction->setShortcutContext(Qt::ApplicationShortcut);
    m_resetAllLinePlotsAction->setStatusTip("Clear history in every visible line plot");
    connect(m_resetAllLinePlotsAction, &QAction::triggered, this, &MainWindow::OnResetAllLinePlots);

    m_clearLinePlotsOnRewindAction = viewMenu->addAction("Clear line plots on rewind-to-start");
    m_clearLinePlotsOnRewindAction->setCheckable(true);
    m_clearLinePlotsOnRewindAction->setStatusTip("Clear line plots when rewind-to-start is used");
    connect(
        m_clearLinePlotsOnRewindAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            m_clearLinePlotsOnRewind = checked;

            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/clearLinePlotsOnRewind", checked);
        }
    );

    m_clearLinePlotsOnBackwardSeekAction = viewMenu->addAction("Clear line plots on backward seek");
    m_clearLinePlotsOnBackwardSeekAction->setCheckable(true);
    m_clearLinePlotsOnBackwardSeekAction->setStatusTip("Clear line plots whenever replay moves backward in time");
    connect(
        m_clearLinePlotsOnBackwardSeekAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            m_clearLinePlotsOnBackwardSeek = checked;

            QSettings settings("SmartDashboard", "SmartDashboardApp");
            settings.setValue("replay/clearLinePlotsOnBackwardSeek", checked);
        }
    );

    m_statusLabel = new QLabel("State: Disconnected", this);
    statusBar()->addPermanentWidget(m_statusLabel);

    m_playbackCursorStatusLabel = new QLabel("t=0.000s", this);
    m_playbackCursorStatusLabel->setStyleSheet("QLabel { color: #b8b8b8; }");
    statusBar()->addPermanentWidget(m_playbackCursorStatusLabel);

    m_playbackWindowStatusLabel = new QLabel("window=0.000s", this);
    m_playbackWindowStatusLabel->setStyleSheet("QLabel { color: #9aa4b2; }");
    statusBar()->addPermanentWidget(m_playbackWindowStatusLabel);

    QMenu* connectionMenu = menuBar()->addMenu("&Connection");
    auto* transportActionGroup = new QActionGroup(this);
    transportActionGroup->setExclusive(true);
    m_connectTransportAction = connectionMenu->addAction("Connect");
    m_disconnectTransportAction = connectionMenu->addAction("Disconnect");
    connectionMenu->addSeparator();
    m_useDirectTransportAction = connectionMenu->addAction("Use Direct transport");
    m_useDirectTransportAction->setCheckable(true);
    m_useDirectTransportAction->setData(QStringLiteral("direct"));
    transportActionGroup->addAction(m_useDirectTransportAction);

    for (const sd::transport::TransportDescriptor& descriptor : m_transportRegistry.GetAvailableTransports())
    {
        if (descriptor.id == "direct" || descriptor.id == "replay")
        {
            continue;
        }

        QAction* action = connectionMenu->addAction(QString("Use %1 transport").arg(descriptor.displayName));
        action->setCheckable(true);
        action->setData(descriptor.id);
        transportActionGroup->addAction(action);
        m_pluginTransportActions.push_back(action);
        connect(
            action,
            &QAction::triggered,
            this,
            [this, descriptorId = descriptor.id]()
            {
                SelectTransport(descriptorId);
            }
        );
    }

    m_useReplayTransportAction = connectionMenu->addAction("Use Replay transport");
    m_useReplayTransportAction->setCheckable(true);
    m_useReplayTransportAction->setData(QStringLiteral("replay"));
    transportActionGroup->addAction(m_useReplayTransportAction);
    connectionMenu->addSeparator();
    m_editTransportSettingsAction = connectionMenu->addAction("Transport Settings...");

    connect(m_connectTransportAction, &QAction::triggered, this, &MainWindow::OnConnectTransport);
    connect(m_disconnectTransportAction, &QAction::triggered, this, &MainWindow::OnDisconnectTransport);
    connect(m_useDirectTransportAction, &QAction::triggered, this, &MainWindow::OnUseDirectTransport);
    connect(m_useReplayTransportAction, &QAction::triggered, this, &MainWindow::OnUseReplayTransport);
    connect(m_telemetryFeatureViewAction, &QAction::triggered, this, &MainWindow::OnToggleTelemetryFeature);
    connect(m_editTransportSettingsAction, &QAction::triggered, this, &MainWindow::OnEditTransportSettings);

    m_telemetryControlsPanel = new QWidget(this);
    auto* playbackLayout = new QHBoxLayout(m_telemetryControlsPanel);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    playbackLayout->setSpacing(6);
    playbackLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

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

    auto* replayControlsHost = new QWidget(m_replayControlsDock);
    auto* replayControlsHostLayout = new QVBoxLayout(replayControlsHost);
    replayControlsHostLayout->setContentsMargins(0, 0, 0, 0);
    replayControlsHostLayout->setSpacing(0);
    replayControlsHostLayout->addWidget(m_telemetryControlsPanel, 0, Qt::AlignTop);
    replayControlsHostLayout->addStretch(1);
    m_replayControlsDock->setWidget(replayControlsHost);
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
    UpdateReplayDockHeightLock();
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
            menu.addSeparator();
            QAction* resetAction = menu.addAction("Reset Replay Layout");

            QAction* chosen = menu.exec(m_replayControlsDock->mapToGlobal(pos));
            if (chosen == nullptr)
            {
                return;
            }

            if (chosen == resetAction)
            {
                RestoreDefaultReplayWorkspaceLayout();
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
            UpdateReplayDockHeightLock();
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
            menu.addSeparator();
            QAction* resetAction = menu.addAction("Reset Replay Layout");

            QAction* chosen = menu.exec(m_replayTimelineDock->mapToGlobal(pos));
            if (chosen == nullptr)
            {
                return;
            }

            if (chosen == resetAction)
            {
                RestoreDefaultReplayWorkspaceLayout();
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
            UpdateReplayDockHeightLock();
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

            if (!QCoreApplication::closingDown()
                && isVisible()
                && m_replayControlsViewAction != nullptr
                && m_replayControlsViewAction->isEnabled())
            {
                m_replayControlsPreferredVisible = visible;
                QSettings settings("SmartDashboard", "SmartDashboardApp");
                settings.setValue("replay/controlsVisible", m_replayControlsPreferredVisible);
            }

            if (m_replayControlsViewAction != nullptr)
            {
                const bool prior = m_replayControlsViewAction->blockSignals(true);
                m_replayControlsViewAction->setChecked(visible);
                m_replayControlsViewAction->blockSignals(prior);
            }
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

            if (!QCoreApplication::closingDown()
                && isVisible()
                && m_replayTimelineViewAction != nullptr
                && m_replayTimelineViewAction->isEnabled())
            {
                m_replayTimelinePreferredVisible = visible;
                QSettings settings("SmartDashboard", "SmartDashboardApp");
                settings.setValue("replay/timelineVisible", m_replayTimelinePreferredVisible);
            }

            if (m_replayTimelineViewAction != nullptr)
            {
                const bool prior = m_replayTimelineViewAction->blockSignals(true);
                m_replayTimelineViewAction->setChecked(visible);
                m_replayTimelineViewAction->blockSignals(prior);
            }
        }
    );
    connect(
        m_replayTimelineDock,
        &QDockWidget::topLevelChanged,
        this,
        [this](bool topLevel)
        {
            SetTimelineDockMode(m_playbackTimeline, topLevel);
            UpdateReplayDockHeightLock();
        }
    );
    connect(
        m_replayControlsDock,
        &QDockWidget::topLevelChanged,
        this,
        [this](bool)
        {
            UpdateReplayDockHeightLock();
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

    // Ian: Run Browser dock — dockable tree view for browsing signal key
    // hierarchies in loaded replay files.  Starts hidden on first-ever launch;
    // subsequent launches restore visibility via Qt's saveState/restoreState.
    // When a replay transport starts, PopulateRunBrowserFromReplayFile() shows
    // it automatically and restores persisted checked/expanded state.
    m_runBrowserDock = new sd::widgets::RunBrowserDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_runBrowserDock);
    m_runBrowserDock->setVisible(false);
    connect(
        m_runBrowserViewAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            if (m_runBrowserDock != nullptr)
            {
                m_runBrowserDock->setVisible(checked);
            }
        }
    );
    connect(
        m_runBrowserDock,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            if (m_runBrowserViewAction != nullptr)
            {
                const bool prior = m_runBrowserViewAction->blockSignals(true);
                m_runBrowserViewAction->setChecked(visible);
                m_runBrowserViewAction->blockSignals(prior);
            }
        }
    );

    // Ian: When the user checks/unchecks groups in the Run Browser, show or
    // hide tiles on the dashboard canvas.  The tiles are created by the
    // transport pipeline — the Run Browser only controls visibility.
    connect(
        m_runBrowserDock,
        &sd::widgets::RunBrowserDock::CheckedSignalsChanged,
        this,
        [this](const QSet<QString>& checkedKeys, const QMap<QString, QString>& keyToType)
        {
            OnRunBrowserCheckedSignalsChanged(checkedKeys, keyToType);
        }
    );

    // Ian: Layout-mirror wiring.  The Run Browser dock (streaming mode) builds
    // its tree as a 1:1 mirror of whatever tiles exist on the layout.  These
    // signals decouple the dock from transport details — it never listens to
    // the transport directly.  Tiles loaded from saved layouts, created by any
    // transport, or added by future mechanisms all flow through here.
    connect(this, &MainWindow::TileAdded, m_runBrowserDock, &sd::widgets::RunBrowserDock::OnTileAdded);
    connect(this, &MainWindow::TileRemoved, m_runBrowserDock, &sd::widgets::RunBrowserDock::OnTileRemoved);
    // Ian: TilesCleared → ClearDiscoveredKeys handles the user's "Clear All"
    // action during streaming mode.  ClearDiscoveredKeys is a no-op when not
    // in streaming mode, so layout operations during replay (Clear Widgets,
    // Load Layout) leave the file-driven tree untouched.
    connect(this, &MainWindow::TilesCleared, m_runBrowserDock, &sd::widgets::RunBrowserDock::ClearDiscoveredKeys);

    // Ian: Camera viewer dock — dockable MJPEG stream viewer.  Follows the
    // same creation/wiring pattern as RunBrowserDock above: starts hidden,
    // View menu action toggles visibility, visibilityChanged syncs the action
    // state using blockSignals to avoid infinite loops.
    m_cameraDock = new sd::widgets::CameraViewerDock(this);
    addDockWidget(Qt::RightDockWidgetArea, m_cameraDock);
    m_cameraDock->setVisible(false);
    connect(
        m_cameraViewAction,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            if (m_cameraDock != nullptr)
            {
                m_cameraDock->setVisible(checked);
            }
        }
    );
    connect(
        m_cameraDock,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            if (m_cameraViewAction != nullptr)
            {
                const bool prior = m_cameraViewAction->blockSignals(true);
                m_cameraViewAction->setChecked(visible);
                m_cameraViewAction->blockSignals(prior);
            }
        }
    );

    // Ian: Camera discovery — abstracted from transport via aggregator.
    // The aggregator merges cameras from all discovery providers and feeds
    // the unified list to the dock.  This decouples camera discovery from
    // which transport is active (Direct, Native Link, NT4, or none).
    // The protocol discovery source uses an empty tag so cameras appear
    // undecorated in the UI (e.g. "SimCamera" not "[NT4] SimCamera") —
    // the user doesn't care which transport discovered the camera.
    // Developer-level tracing is available via OutputDebugString in the
    // aggregator, visible in the Visual Studio Output window.
    m_cameraDiscovery = new sd::camera::CameraPublisherDiscovery(this);
    m_staticCameraSource = new sd::camera::StaticCameraSource(this);
    m_staticCameraSource->LoadFromSettings();

    m_cameraAggregator = new sd::camera::CameraDiscoveryAggregator(this);
    m_cameraAggregator->AddSource(QString(), m_cameraDiscovery);
    m_cameraAggregator->AddSource(QStringLiteral("Static"), m_staticCameraSource);

    // Ian: Wire aggregator -> dock.  The dock receives camera names
    // (e.g. "SimCamera", "[Static] ShopCam") and doesn't know which
    // provider they came from.
    connect(
        m_cameraAggregator,
        &sd::camera::CameraDiscoveryAggregator::CameraDiscovered,
        m_cameraDock,
        &sd::widgets::CameraViewerDock::AddDiscoveredCamera
    );
    connect(
        m_cameraAggregator,
        &sd::camera::CameraDiscoveryAggregator::CamerasCleared,
        m_cameraDock,
        &sd::widgets::CameraViewerDock::ClearDiscoveredCameras
    );
    connect(
        m_cameraAggregator,
        &sd::camera::CameraDiscoveryAggregator::CameraRemoved,
        m_cameraDock,
        &sd::widgets::CameraViewerDock::RemoveDiscoveredCamera
    );

    // Ian: Wire dock "Save" button -> static camera source.
    connect(
        m_cameraDock,
        &sd::widgets::CameraViewerDock::SaveStaticCameraRequested,
        m_staticCameraSource,
        &sd::camera::StaticCameraSource::AddCamera
    );

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    const int persistedKindValue = settings.value("connection/transportKind", static_cast<int>(sd::transport::TransportKind::Direct)).toInt();
    m_connectionConfig.kind = static_cast<sd::transport::TransportKind>(persistedKindValue);
    m_connectionConfig.transportId = settings.value("connection/transportId").toString().trimmed();
    if (m_connectionConfig.transportId.isEmpty())
    {
        if (persistedKindValue == 2)
        {
            m_connectionConfig.transportId = "replay";
        }
        else if (persistedKindValue == 1)
        {
            m_connectionConfig.transportId = "legacy-nt";
        }
        else
        {
            m_connectionConfig.transportId = "direct";
        }
    }
    m_connectionConfig.ntHost = settings.value("connection/ntHost", "127.0.0.1").toString();
    m_connectionConfig.ntTeam = settings.value("connection/ntTeam", 0).toInt();
    m_connectionConfig.ntUseTeam = settings.value("connection/ntUseTeam", true).toBool();
    m_connectionConfig.ntClientName = settings.value("connection/ntClientName", "SmartDashboardApp").toString();
    m_connectionConfig.pluginSettingsJson = settings.value("connection/pluginSettingsJson").toString();
    m_connectionConfig.replayFilePath = settings.value("connection/replayFilePath").toString();
    SyncConnectionConfigFromPluginSettingsJson();
    if (GetSelectedTransportDescriptor() == nullptr)
    {
        SelectTransport("direct");
    }
    else
    {
        m_connectionConfig.kind = GetSelectedTransportDescriptor()->kind;
    }
    m_telemetryFeatureEnabled = settings.value("telemetry/enabled", true).toBool();
    m_recordRequested = settings.value("telemetry/recordEnabled", false).toBool();
    m_replayControlsPreferredVisible = settings.value("replay/controlsVisible", true).toBool();
    m_replayTimelinePreferredVisible = settings.value("replay/timelineVisible", true).toBool();
    m_replayMarkersPreferredVisible = settings.value("replay/markersVisible", true).toBool();
    m_clearLinePlotsOnRewind = settings.value("replay/clearLinePlotsOnRewind", false).toBool();
    m_clearLinePlotsOnBackwardSeek = settings.value("replay/clearLinePlotsOnBackwardSeek", false).toBool();

    // Ian: Load persisted Run Browser state.  The checked keys are loaded into
    // m_runBrowserCheckedKeys now (before StartTransport) so that GetOrCreateTile
    // can correctly hide/show tiles as the transport creates them.  The actual
    // tree population and visual state (expanded paths) is applied later in
    // PopulateRunBrowserFromReplayFile(), called from StartTransport().
    LoadRunBrowserState();
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
    if (m_clearLinePlotsOnRewindAction != nullptr)
    {
        m_clearLinePlotsOnRewindAction->setChecked(m_clearLinePlotsOnRewind);
    }
    if (m_clearLinePlotsOnBackwardSeekAction != nullptr)
    {
        m_clearLinePlotsOnBackwardSeekAction->setChecked(m_clearLinePlotsOnBackwardSeek);
    }
    if (m_recordButton != nullptr)
    {
        m_recordButton->setChecked(m_recordRequested);
    }

    if (CurrentTransportUsesRememberedControlValues())
    {
        LoadRememberedControlValues();
    }
    else
    {
        m_rememberedControlValues.clear();
    }

    SetTimelineDockMode(m_playbackTimeline, m_replayTimelineDock != nullptr && m_replayTimelineDock->isFloating());

    LoadUserReplayBookmarks();
    ApplyTransportMenuChecks();

    m_playbackUiTimer = new QTimer(this);
    m_playbackUiTimer->setInterval(100);
    connect(m_playbackUiTimer, &QTimer::timeout, this, &MainWindow::UpdatePlaybackUiState);
    m_playbackUiTimer->start();
    UpdatePlaybackUiState();

    // Ian: Host-level auto-reconnect timer.  When a direct transport fires
    // Disconnected and auto-connect is enabled (and the user did not manually
    // click Disconnect), this timer retries by calling StartTransport().
    // The interval is deliberately short (1 s) so that a robot-code restart
    // re-establishes the dashboard quickly.  The timer is single-shot so we
    // get exactly one reconnect attempt per expiry; OnConnectionStateChanged
    // restarts it if the attempt fails again.
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(1000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::OnReconnectTimerFired);

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
    if (CurrentTransportUsesRememberedControlValues())
    {
        ApplyRememberedControlValuesToTiles();
    }
    ApplyTemporaryDefaultValuesToTiles();
    LoadWindowGeometry();

#ifdef _DEBUG
    // Debug-only named-pipe command channel so automation scripts can reliably
    // trigger connect/disconnect without fragile keyboard injection.
    // Channel name: "SmartDashboardApp_DebugCmd_<PID>"
    // Supported commands (newline-terminated): "disconnect\n", "connect\n"
    {
        const QString serverName = QString("SmartDashboardApp_DebugCmd_%1")
            .arg(static_cast<qint64>(QCoreApplication::applicationPid()));
        m_debugCommandServer = new QLocalServer(this);
        QLocalServer::removeServer(serverName); // clean up any stale socket
        if (m_debugCommandServer->listen(serverName))
        {
            DebugLogUiEvent(QString("debug_cmd_server=listening name=%1").arg(serverName));
            connect(m_debugCommandServer, &QLocalServer::newConnection,
                    this, &MainWindow::OnDebugCommandReceived);
        }
        else
        {
            DebugLogUiEvent(QString("debug_cmd_server=failed name=%1 err=%2")
                .arg(serverName)
                .arg(m_debugCommandServer->errorString()));
        }
    }
#endif

    if (startTransportOnInit)
    {
        StartTransport();
    }
}

MainWindow::~MainWindow()
{
    DebugLogUiEvent("MainWindow stop");
    StopTransport();
}

void MainWindow::DebugLogUiEvent(const QString& line) const
{
    if (!m_uiDebugLog.is_open())
    {
        const QString tag = QProcessEnvironment::systemEnvironment().value("SMARTDASHBOARD_INSTANCE_TAG").trimmed();
        QString resolvedTag = tag;
        if (resolvedTag.isEmpty())
        {
            const QStringList args = QCoreApplication::arguments();
            for (int i = 0; i + 1 < args.size(); ++i)
            {
                if (args[i] == "--instance-tag")
                {
                    resolvedTag = args[i + 1].trimmed();
                    break;
                }
            }
        }
        if (resolvedTag.isEmpty())
        {
            return;
        }

        // Ian: The shared-state probe needs one log file per dashboard
        // process. We keep a last-resort argv fallback here because the probe's
        // entire value is proving that the second process observed the same
        // retained state, not just that it launched.
        const QString logPath = sd::app::GetDebugLogPath(QString("native_link_ui_%1.log").arg(resolvedTag));
        sd::app::AppendTaggedDebugLine("native_link_startup", resolvedTag, QString("ui_log_open=%1").arg(logPath));
        m_uiDebugLog.open(logPath.toStdString(), std::ios::out | std::ios::app);
        if (!m_uiDebugLog.is_open())
        {
            sd::app::AppendTaggedDebugLine("native_link_startup", resolvedTag, "ui_log_open_failed");
            return;
        }
        sd::app::AppendTaggedDebugLine("native_link_startup", resolvedTag, "ui_log_opened");
    }

    m_uiDebugLog << line.toStdString() << std::endl;
}

void MainWindow::DrainPendingUiUpdates()
{
    // Ian: Per-call budget cap (Fix B).  During auton mode the robot publishes
    // ~25 keys at ~62 Hz (~1,500 msg/sec over TCP).  Without a cap, a burst
    // fills m_pendingUiUpdates with thousands of items and DrainPendingUiUpdates
    // processes them all in one synchronous pass on the UI thread, blocking
    // painting and input for 10-20 seconds — the freeze + playback-backlog
    // symptom observed in manual testing.  Processing at most kDrainBudget
    // items per event-loop turn bounds the UI-thread hold time to a few ms.
    // If the queue still has items after the budget is consumed, a new drain
    // is scheduled immediately so no updates are lost, just slightly deferred.
    static constexpr int kDrainBudget = 150;

    QVector<sd::transport::VariableUpdate> batch;
    bool reschedule = false;
    {
        std::lock_guard<std::mutex> lock(m_pendingUiUpdatesMutex);
        m_uiDrainScheduled = false;

        const int take = std::min(static_cast<int>(m_pendingUiUpdates.size()), kDrainBudget);
        batch = QVector<sd::transport::VariableUpdate>(
            m_pendingUiUpdates.begin(),
            m_pendingUiUpdates.begin() + take);
        m_pendingUiUpdates.erase(m_pendingUiUpdates.begin(), m_pendingUiUpdates.begin() + take);

        if (!m_pendingUiUpdates.isEmpty())
        {
            m_uiDrainScheduled = true;
            reschedule = true;
        }
    }

    if (reschedule)
    {
        QMetaObject::invokeMethod(this, &MainWindow::DrainPendingUiUpdates, Qt::QueuedConnection);
    }

    // Ian: Last-write-wins coalescing per key (Fix C).  When the robot sends
    // multiple updates for the same key within one budget window (common at
    // 62 Hz with 25 keys), only the most recent value matters for display.
    // Build an ordered list of unique keys, keeping the last-seen VariableUpdate
    // for each, in first-appearance order so tile creation order is stable.
    QVector<sd::transport::VariableUpdate> coalesced;
    coalesced.reserve(batch.size());
    std::unordered_map<std::string, int> keyIndex;  // key -> index into coalesced
    keyIndex.reserve(static_cast<size_t>(batch.size()));
    for (const auto& update : batch)
    {
        const std::string key = update.key.toStdString();
        auto it = keyIndex.find(key);
        if (it == keyIndex.end())
        {
            keyIndex[key] = static_cast<int>(coalesced.size());
            coalesced.push_back(update);
        }
        else
        {
            coalesced[it->second] = update;  // overwrite with newer value
        }
    }

    for (const auto& update : coalesced)
    {
        OnVariableUpdateReceived(update.key, update.valueType, update.value, static_cast<quint64>(update.seq));
    }
}

void MainWindow::OnToggleEditable()
{
    m_isEditable = m_editableAction->isChecked();

    if (!m_isEditable)
    {
        ClearTileSelection();
    }

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
    if (IsHarnessFocusKey(key))
    {
        QString valueText;
        if (value.canConvert<QStringList>())
        {
            valueText = value.toStringList().join(",");
        }
        else
        {
            valueText = value.toString();
        }

        DebugLogUiEvent(QString("update key=%1 value=%2 seq=%3")
            .arg(key, valueText, QString::number(seq)));
    }

    // Ian: Forward every variable update to CameraPublisherDiscovery.  It
    // filters internally for /CameraPublisher/ keys and ignores everything
    // else, so the cost is a cheap string prefix check per update.
    if (m_cameraDiscovery != nullptr)
    {
        m_cameraDiscovery->OnVariableUpdate(key, valueType, value);
    }

    if (valueType == static_cast<int>(sd::direct::ValueType::String)
        || valueType == static_cast<int>(sd::direct::ValueType::StringArray))
    {
        if (m_connectionConfig.kind == sd::transport::TransportKind::Direct && key == "AutonTest")
        {
            return;
        }

        if (CurrentTransportSupportsChooser() && key.endsWith("/.type") && value.toString() == "String Chooser")
        {
            const QString chooserBase = key.left(key.length() - QString("/.type").length());
            sd::widgets::VariableTile* chooserTile = GetOrCreateTile(chooserBase, sd::widgets::VariableType::String);
            if (chooserTile != nullptr)
            {
                if (chooserTile->GetWidgetType() != "string.chooser")
                {
                    chooserTile->SetWidgetType("string.chooser");
                }
                chooserTile->SetStringChooserMode(true);
                chooserTile->SetTitleText(BuildDisplayLabel(chooserBase));
            }
            return;
        }

        if (CurrentTransportSupportsChooser() && key.endsWith("/options"))
        {
            const QString chooserBase = key.left(key.length() - QString("/options").length());
            sd::widgets::VariableTile* chooserTile = GetOrCreateTile(chooserBase, sd::widgets::VariableType::String);
            if (chooserTile != nullptr)
            {
                if (chooserTile->GetWidgetType() != "string.chooser")
                {
                    chooserTile->SetWidgetType("string.chooser");
                }
                chooserTile->SetStringChooserMode(true);
                chooserTile->SetTitleText(BuildDisplayLabel(chooserBase));

                QStringList rawOptions;
                if (value.canConvert<QStringList>())
                {
                    rawOptions = value.toStringList();
                }
                else
                {
                    rawOptions = value.toString().split(',', Qt::SkipEmptyParts);
                }

                QStringList options;
                options.reserve(rawOptions.size());
                for (const QString& raw : rawOptions)
                {
                    const QString trimmed = raw.trimmed();
                    if (!trimmed.isEmpty())
                    {
                        options.push_back(trimmed);
                    }
                }
                chooserTile->SetStringChooserOptions(options);
            }
            return;
        }

        if (CurrentTransportSupportsChooser() && (key.endsWith("/active") || key.endsWith("/selected") || key.endsWith("/default")))
        {
            QString suffix = "/selected";
            if (key.endsWith("/active"))
            {
                suffix = "/active";
            }
            else if (key.endsWith("/default"))
            {
                suffix = "/default";
            }
            const QString chooserBase = key.left(key.length() - suffix.length());
            sd::widgets::VariableTile* chooserTile = GetOrCreateTile(chooserBase, sd::widgets::VariableType::String);
            if (chooserTile != nullptr)
            {
                if (chooserTile->GetWidgetType() != "string.chooser")
                {
                    chooserTile->SetWidgetType("string.chooser");
                }
                chooserTile->SetStringChooserMode(true);
                chooserTile->SetTitleText(BuildDisplayLabel(chooserBase));

                if (suffix == "/selected")
                {
                    chooserTile->SetStringValue(value.toString());

                    // In Direct mode the robot reads chooser intent from the
                    // dashboard->robot command channel, not from telemetry.
                    // Mirror retained/live chooser selection updates into the
                    // command channel so a fresh auton enable sees the latest
                    // operator intent even if no local edit happened this run.
                    if (m_connectionConfig.kind == sd::transport::TransportKind::Direct && m_transport)
                    {
                        m_transport->PublishString(chooserBase + "/selected", value.toString());
                    }
                }
            }

            return;
        }
    }

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

    // Ian: Multi-line plot fan-out.  If this key has been absorbed into
    // another tile's multi-line plot, forward the double value to the
    // owning plot tile as an additional sample.  The standalone tile still
    // exists (hidden) and gets its value above; this adds the data point
    // to the composite plot's series for that key.
    if (record.type == sd::widgets::VariableType::Double)
    {
        auto ownerIt = m_plotSourceOwners.find(keyStd);
        if (ownerIt != m_plotSourceOwners.end() && ownerIt->second != nullptr)
        {
            ownerIt->second->AddSampleToPlotSource(key, record.value.toDouble());
        }
    }

    if (m_connectionConfig.kind == sd::transport::TransportKind::Direct
        && m_transport
        && IsOperatorControlWidget(tile))
    {
        // Ian: This branch exists so Direct mode can still mirror retained/live
        // operator widgets back onto the command channel when the old workflow
        // expects it. Do not treat incoming telemetry as SmartDashboard-owned
        // persistence here. Only explicit local edits are allowed to create or
        // refresh `m_rememberedControlValues`, or retained authority truth like
        // `TestMove=3.5` will start masquerading as a local startup setting.
        if (record.type == sd::widgets::VariableType::Bool)
        {
            m_transport->PublishBool(key, record.value.toBool());
        }
        else if (record.type == sd::widgets::VariableType::Double)
        {
            m_transport->PublishDouble(key, record.value.toDouble());
        }
        else if (record.type == sd::widgets::VariableType::String && tile->GetWidgetType() != "string.chooser")
        {
            m_transport->PublishString(key, record.value.toString());
        }
    }

    RecordVariableEvent(key, valueType, value, seq);
}

void MainWindow::OnConnectionStateChanged(int state)
{
    m_connectionState = state;

    QString stateText = "Disconnected";
    if (state == static_cast<int>(sd::transport::ConnectionState::Connecting))
    {
        stateText = "Connecting";
    }
    else if (state == static_cast<int>(sd::transport::ConnectionState::Connected))
    {
        stateText = "Connected";
    }
    DebugLogUiEvent(QString("connection_state=%1").arg(stateText));

    const int connected = static_cast<int>(sd::transport::ConnectionState::Connected);
    const int disconnected = static_cast<int>(sd::transport::ConnectionState::Disconnected);

    if (state == connected)
    {
        // Ian: Successful connection — cancel any pending reconnect attempt.
        // The timer is single-shot so it won't fire again on its own, but
        // stopping it explicitly handles the narrow window where the timer has
        // expired but the queued slot hasn't executed yet.
        m_reconnectTimer->stop();
        m_userDisconnected = false;

        // Reconnect handling: reset sequence gating when transport re-enters connected state.
        m_variableStore.ResetSequenceTracking();

        // Ian: The merged replay/line-plot work changed more startup behavior in
        // the window, but Native Link still needs one deterministic reconnect
        // rule: accept the authority-owned retained snapshot first, then publish
        // dashboard-owned remembered controls. Doing this in the explicit
        // `Connected` callback avoids a race where one dashboard instance can
        // restart and republish before another instance has finished applying its
        // snapshot-driven tiles.
        PublishRememberedControlValues();
        RepublishPluginControlEdits();
    }
    else if (state == disconnected)
    {
        // Ian: Host-level auto-reconnect: when a plugin fires Disconnected
        // (either a failed connect attempt or a dropped connection), the host
        // schedules a retry via m_reconnectTimer — but only if:
        //   1. auto_connect is enabled in settings
        //   2. the user didn't manually click Disconnect
        //   3. we're using a direct (non-replay) transport
        // The timer is single-shot; each expiry triggers one Stop+Start cycle.
        // If that attempt also fails (plugin fires Disconnected again), we end
        // up back here and schedule another retry — creating a host-driven
        // retry loop without any per-plugin reconnect code.
        if (IsAutoConnectEnabled()
            && !m_userDisconnected
            && m_connectionConfig.kind != sd::transport::TransportKind::Replay)
        {
            if (!m_reconnectTimer->isActive())
            {
                m_reconnectTimer->start();
            }
        }

        // Ian: On disconnect, clear protocol-discovered cameras so stale entries
        // don't linger in the combo.  Static cameras survive — they're not
        // tied to the transport.  The aggregator handles removing only the
        // protocol provider's cameras and emitting appropriate signals to the dock.
        if (m_cameraDiscovery != nullptr)
        {
            m_cameraDiscovery->Clear();
        }
    }

    UpdateWindowConnectionText(state);
    RecordConnectionEvent(state);

    // Refresh Connect/Disconnect enabled state now that m_connectionState has changed.
    ApplyTransportMenuChecks();
}

void MainWindow::PublishRememberedControlValues()
{
    if (!m_transport || !CurrentTransportUsesRememberedControlValues())
    {
        return;
    }

    for (const auto& [keyStd, remembered] : m_rememberedControlValues)
    {
        const QString key = QString::fromStdString(keyStd);
        if (key.isEmpty())
        {
            continue;
        }

        if (remembered.valueType == static_cast<int>(sd::direct::ValueType::Bool))
        {
            m_transport->PublishBool(key, remembered.value.toBool());
        }
        else if (remembered.valueType == static_cast<int>(sd::direct::ValueType::Double))
        {
            m_transport->PublishDouble(key, remembered.value.toDouble());
        }
        else if (remembered.valueType == static_cast<int>(sd::direct::ValueType::String))
        {
            const auto chooserIt = m_tiles.find(keyStd);
            if (chooserIt != m_tiles.end() && chooserIt->second != nullptr && chooserIt->second->GetWidgetType() == "string.chooser")
            {
                m_transport->PublishString(key + "/selected", remembered.value.toString());
            }
            else
            {
                m_transport->PublishString(key, remembered.value.toString());
            }
        }
    }
}

// Ian: On plugin transport reconnect the server's snapshot resets controls to
// their seed defaults (e.g. /selected -> "Do Nothing", doubles -> 0.0).
// This method re-publishes *all* operator control edits — choosers, doubles,
// bools, and plain strings — captured by the three OnControl*Edited handlers,
// so the operator's intent survives a DS restart cycle.  Called from
// OnConnectionStateChanged(Connected), which fires after the snapshot has
// already been consumed (see native_link_tcp_client.cpp: Connected fires
// after __live_begin__).
void MainWindow::RepublishPluginControlEdits()
{
    if (!m_transport || m_connectionConfig.kind != sd::transport::TransportKind::Plugin)
    {
        return;
    }

    for (const auto& [keyStd, edit] : m_pluginControlEdits)
    {
        const QString key = QString::fromStdString(keyStd);
        if (key.isEmpty())
        {
            continue;
        }

        const auto tileIt = m_tiles.find(keyStd);
        sd::widgets::VariableTile* tile = (tileIt != m_tiles.end()) ? tileIt->second : nullptr;

        if (edit.valueType == static_cast<int>(sd::direct::ValueType::Bool))
        {
            const bool boolVal = edit.value.toBool();
            if (tile) { tile->SetBoolValue(boolVal); }
            m_transport->PublishBool(key, boolVal);
            DebugLogUiEvent(QString("republish_plugin_control key=%1 bool=%2").arg(key).arg(boolVal));
        }
        else if (edit.valueType == static_cast<int>(sd::direct::ValueType::Double))
        {
            const double dblVal = edit.value.toDouble();
            if (tile) { tile->SetDoubleValue(dblVal); }
            m_transport->PublishDouble(key, dblVal);
            DebugLogUiEvent(QString("republish_plugin_control key=%1 double=%2").arg(key).arg(dblVal));
        }
        else if (edit.valueType == static_cast<int>(sd::direct::ValueType::String))
        {
            const QString strVal = edit.value.toString();
            if (strVal.isEmpty()) { continue; }

            // Ian: Choosers publish to /selected (a sub-key), not to the
            // base key.  Non-chooser strings publish to the key directly.
            if (tile && tile->GetWidgetType() == "string.chooser")
            {
                tile->SetStringValue(strVal);
                m_transport->PublishString(key + "/selected", strVal);
                DebugLogUiEvent(QString("republish_plugin_control key=%1 chooser=%2").arg(key, strVal));
            }
            else
            {
                if (tile) { tile->SetStringValue(strVal); }
                m_transport->PublishString(key, strVal);
                DebugLogUiEvent(QString("republish_plugin_control key=%1 string=%2").arg(key, strVal));
            }
        }
    }
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
    ClearTileSelection();
    for (auto& [_, tile] : m_tiles)
    {
        if (tile != nullptr)
        {
            tile->deleteLater();
        }
    }

    m_tiles.clear();
    m_plotSourceOwners.clear();
    m_savedLayoutByKey.clear();
    m_variableStore.Clear();
    m_pluginControlEdits.clear();
    m_nextTileOffset = 0;
    m_lastTransportSeq = 0;
    m_runBrowserHiddenKeys.clear();
    MarkLayoutDirty();

    // Ian: Hard-reset the Run Browser.  ClearAllRuns() wipes both replay
    // runs and streaming state regardless of the current mode.  This is
    // intentional — "Clear Widgets" means the user wants a blank slate,
    // including the Run Browser tree.  ClearDiscoveredKeys (fired by
    // TilesCleared below) has a streaming-mode guard that would skip the
    // clear when coming from replay mode, so ClearAllRuns is the only
    // reliable way to guarantee the tree is empty.
    //
    // If a non-replay transport is still connected, StartTransport (or
    // incoming data via GetOrCreateTile → TileAdded) will re-enter
    // streaming mode and repopulate the tree naturally.
    if (m_runBrowserDock != nullptr)
    {
        m_runBrowserDock->ClearAllRuns();

        // Ian: Re-enter streaming mode whenever the selected transport
        // is non-replay.  This lets subsequent operations (Load Layout,
        // incoming transport keys) repopulate the Run Browser tree via
        // GetOrCreateTile → TileAdded → OnTileAdded.  Without this,
        // ClearAllRuns leaves m_streamingMode false and OnTileAdded
        // silently ignores every key.
        //
        // We check the *configured* transport kind, not whether
        // m_transport is non-null, because the user may have switched
        // to NT4/Direct but not yet clicked Connect — the layout load
        // that follows still needs the tree to mirror the tiles.
        if (m_connectionConfig.kind != sd::transport::TransportKind::Replay)
        {
            m_runBrowserDock->SetStreamingRootLabel(GetSelectedTransportDisplayName());
        }
    }

    // Ian: Notify observers that all tiles were removed.  The
    // ClearDiscoveredKeys handler is a harmless no-op here because
    // ClearAllRuns already reset everything (and SetStreamingRootLabel,
    // if called, re-initialized streaming state cleanly).
    emit TilesCleared();
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
    connect(tile, &sd::widgets::VariableTile::HideRequested, this, &MainWindow::OnHideTileRequested);
    // Ian: Multi-line plot disband signal.  When the tile switches away from
    // a line plot widget type (or the user removes a source), it emits
    // PlotSourceDisbanded for each absorbed key.  We capture the tile pointer
    // so OnPlotSourceDisbanded knows which plot is disbanding.
    connect(tile, &sd::widgets::VariableTile::PlotSourceDisbanded, this,
        [this, tile](const QString& sourceKey, const QString& originalWidgetType)
        {
            OnPlotSourceDisbanded(tile, sourceKey, originalWidgetType);
        }
    );
    // Ian: Bidirectional visibility sync (Direction A).  When the user toggles
    // per-series visibility in the Properties dialog, update the Run Browser
    // check state to match.  The m_plotSourceOwners guard in
    // OnRunBrowserCheckedSignalsChanged prevents this from feeding back into
    // SetSeriesVisible — the loop terminates cleanly.
    connect(tile, &sd::widgets::VariableTile::PlotSeriesVisibilityChanged, this,
        [this](const QString& sourceKey, bool visible)
        {
            if (m_runBrowserDock == nullptr)
            {
                return;
            }
            // Only sync absorbed keys — the primary series is not in the
            // Run Browser as an absorbed key; it has its own standalone tile.
            if (m_plotSourceOwners.find(sourceKey.toStdString()) == m_plotSourceOwners.end())
            {
                return;
            }
            if (visible)
            {
                m_runBrowserDock->CheckSignalByKey(sourceKey);
            }
            else
            {
                m_runBrowserDock->UncheckSignalByKey(sourceKey);
            }
        }
    );

    tile->setObjectName(QString("tile_%1").arg(QString::number(m_tiles.size() + 1)));
    tile->SetDefaultSize(QSize(220, 84));
    tile->SetEditable(m_isEditable);
    tile->SetSnapToGrid(m_snapToGrid, 8);
    tile->SetEditInteractionMode(m_editInteractionMode);

    auto savedIt = m_savedLayoutByKey.find(keyStd);
    if (savedIt != m_savedLayoutByKey.end() && !IsHarnessFocusKey(key))
    {
        const sd::layout::WidgetLayoutEntry& entry = savedIt->second;
        ApplyLayoutEntryToTile(tile, entry);
    }
    else
    {
        if (key == "Test/AutonTest")
        {
            tile->setGeometry(24, 32, 320, 84);
        }
        else if (key == "TestMove")
        {
            tile->setGeometry(24, 132, 320, 84);
        }
        else if (key == "Timer")
        {
            tile->setGeometry(24, 232, 320, 84);
        }
        else if (key == "Y_ft")
        {
            tile->setGeometry(24, 332, 320, 84);
        }
        else
        {
            tile->setGeometry(24 + m_nextTileOffset, 32 + m_nextTileOffset, 220, 84);
            m_nextTileOffset = (m_nextTileOffset + 24) % 200;
        }
    }

    tile->setProperty("variableKey", key);
    tile->setProperty("widgetType", tile->GetWidgetType());
    tile->installEventFilter(this);

    // Ian: If the Run Browser has an active session (reading or streaming),
    // only show the tile if its key is in the checked set.  An empty checked
    // set with an active session means "nothing checked" — hide all.  When no
    // Run Browser session is active (m_runBrowserActive == false), show
    // everything UNLESS the key is in the layout-persisted hidden set
    // (m_runBrowserHiddenKeys), which carries over from a previously loaded
    // layout file.
    //
    // Layout-mirror mode: the Run Browser tree is populated via the TileAdded
    // signal (emitted below after m_tiles.emplace), not by direct calls here.
    // The dock builds its tree as a 1:1 mirror of whatever tiles exist on the
    // layout.  Groups start checked, so the key will be in
    // m_runBrowserCheckedKeys after the dock emits CheckedSignalsChanged.

    // Ian: If this key has been absorbed into a multi-line plot, hide the
    // standalone tile unconditionally — its data renders inside the owning
    // plot tile.  This takes priority over Run Browser visibility.
    if (m_plotSourceOwners.find(keyStd) != m_plotSourceOwners.end())
    {
        tile->hide();
    }
    else if (m_runBrowserActive)
    {
        if (m_runBrowserCheckedKeys.isEmpty() || !m_runBrowserCheckedKeys.contains(key))
        {
            tile->hide();
        }
        else
        {
            tile->show();
        }
    }
    else if (m_runBrowserHiddenKeys.contains(key))
    {
        tile->hide();
    }
    else
    {
        tile->show();
    }

    m_tiles.emplace(keyStd, tile);

    if (!m_suppressLayoutDirty)
    {
        MarkLayoutDirty();
    }

    // Ian: Notify observers (Run Browser dock) that a tile now exists on the
    // layout.  Emitted after emplace so the tile is findable in m_tiles.
    // The Run Browser uses this to build its streaming-mode tree as a mirror
    // of the layout, rather than listening to raw transport keys.
    {
        QString typeStr;
        switch (type)
        {
            case sd::widgets::VariableType::Bool:   typeStr = QStringLiteral("bool");   break;
            case sd::widgets::VariableType::Double:  typeStr = QStringLiteral("double"); break;
            case sd::widgets::VariableType::String:  typeStr = QStringLiteral("string"); break;
        }
        emit TileAdded(key, typeStr);
    }

    return tile;
}

QString MainWindow::BuildDisplayLabel(const QString& key) const
{
    if (!CurrentTransportUsesShortDisplayKeys())
    {
        return key;
    }

    const QStringList segments = key.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty())
    {
        return key;
    }

    if (segments.size() >= 2 && segments.back() == "AutoChooser")
    {
        return segments[segments.size() - 2];
    }

    return segments.back();
}

void MainWindow::UpdateWindowConnectionText(int state)
{
    m_connectionState = state;
    RefreshWindowTitle();

    const QString transportName = GetSelectedTransportDisplayName();

    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        m_statusLabel->setText(QString("Transport: %1").arg(transportName));
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

    m_statusLabel->setText(QString("Transport: %1 | State: %2").arg(transportName, stateText));
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
    settings.setValue("replay/controlsVisible", m_replayControlsPreferredVisible);
    settings.setValue("replay/timelineVisible", m_replayTimelinePreferredVisible);
    settings.setValue("replay/markersVisible", m_replayMarkersPreferredVisible);

    PersistUserReplayBookmarks();
    PersistRunBrowserState();
}

// Ian: Populate the Run Browser dock from the current replay file path.
// If the dock is already loaded with the same file, skip the reload to avoid
// losing user's current check/expand state.  After (re)loading, restore
// persisted checked keys and expanded paths so the UI looks the same as the
// user left it.
void MainWindow::PopulateRunBrowserFromReplayFile()
{
    if (m_runBrowserDock == nullptr)
    {
        return;
    }

    const QString replayPath = m_connectionConfig.replayFilePath.trimmed();
    if (replayPath.isEmpty())
    {
        return;
    }

    // Avoid redundant reload if the dock already has this file loaded.
    if (m_runBrowserDock->RunCount() > 0 && m_runBrowserDock->GetLoadedFilePath() == replayPath)
    {
        return;
    }

    // Save the checked keys before clearing — ClearAllRuns emits
    // CheckedSignalsChanged with an empty set, which would overwrite
    // m_runBrowserCheckedKeys.
    const QSet<QString> savedCheckedKeys = m_runBrowserCheckedKeys;

    m_runBrowserDock->ClearAllRuns();
    m_runBrowserDock->AddRunFromFile(replayPath);
    m_runBrowserActive = true;

    // Restore persisted checked state (which groups are checked).
    if (!savedCheckedKeys.isEmpty())
    {
        m_runBrowserDock->SetCheckedGroupsBySignalKeys(savedCheckedKeys);
    }

    // Restore persisted expanded/collapsed tree state.
    if (!m_runBrowserExpandedPaths.isEmpty())
    {
        m_runBrowserDock->SetExpandedPaths(m_runBrowserExpandedPaths);
    }

    m_runBrowserDock->show();
    m_runBrowserDock->raise();
}

void MainWindow::PersistRunBrowserState() const
{
    QSettings settings("SmartDashboard", "SmartDashboardApp");

    settings.setValue("runBrowser/active", m_runBrowserActive);

    // Save checked keys as a string list (used by reading mode).
    const QStringList checkedList(m_runBrowserCheckedKeys.begin(), m_runBrowserCheckedKeys.end());
    settings.setValue("runBrowser/checkedKeys", checkedList);

    // Ian: Save streaming-mode hidden keys.  In streaming mode we persist the
    // hidden set (opt-outs) rather than the checked set (which starts as
    // "everything") because that's what we need to re-apply on reconnect.
    if (m_runBrowserDock != nullptr && m_runBrowserDock->IsStreamingModeForTesting())
    {
        const QSet<QString> hidden = m_runBrowserDock->GetHiddenDiscoveredKeys();
        const QStringList hiddenList(hidden.begin(), hidden.end());
        settings.setValue("runBrowser/hiddenKeys", hiddenList);
    }
    else
    {
        const QStringList hiddenList(m_runBrowserHiddenKeys.begin(), m_runBrowserHiddenKeys.end());
        settings.setValue("runBrowser/hiddenKeys", hiddenList);
    }

    // Save expanded paths.
    if (m_runBrowserDock != nullptr)
    {
        settings.setValue("runBrowser/expandedPaths", m_runBrowserDock->GetExpandedPaths());
    }
    else
    {
        settings.setValue("runBrowser/expandedPaths", m_runBrowserExpandedPaths);
    }
}

void MainWindow::LoadRunBrowserState()
{
    QSettings settings("SmartDashboard", "SmartDashboardApp");

    m_runBrowserActive = settings.value("runBrowser/active", false).toBool();

    const QStringList checkedList = settings.value("runBrowser/checkedKeys").toStringList();
    m_runBrowserCheckedKeys = QSet<QString>(checkedList.begin(), checkedList.end());

    const QStringList hiddenList = settings.value("runBrowser/hiddenKeys").toStringList();
    m_runBrowserHiddenKeys = QSet<QString>(hiddenList.begin(), hiddenList.end());

    m_runBrowserExpandedPaths = settings.value("runBrowser/expandedPaths").toStringList();
}

void MainWindow::RestoreDefaultReplayWorkspaceLayout()
{
    if (m_replayControlsDock == nullptr || m_replayTimelineDock == nullptr)
    {
        return;
    }

    m_replayControlsPreferredVisible = true;
    m_replayTimelinePreferredVisible = true;

    m_syncingReplayControlsDockVisibility = true;
    m_syncingReplayTimelineDockVisibility = true;

    m_replayControlsDock->setFloating(false);
    m_replayTimelineDock->setFloating(false);
    addDockWidget(Qt::BottomDockWidgetArea, m_replayControlsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_replayTimelineDock);
    splitDockWidget(m_replayControlsDock, m_replayTimelineDock, Qt::Horizontal);
    m_replayControlsDock->show();
    m_replayTimelineDock->show();

    m_syncingReplayControlsDockVisibility = false;
    m_syncingReplayTimelineDockVisibility = false;

    if (m_replayControlsViewAction != nullptr)
    {
        const bool prior = m_replayControlsViewAction->blockSignals(true);
        m_replayControlsViewAction->setChecked(true);
        m_replayControlsViewAction->blockSignals(prior);
    }
    if (m_replayTimelineViewAction != nullptr)
    {
        const bool prior = m_replayTimelineViewAction->blockSignals(true);
        m_replayTimelineViewAction->setChecked(true);
        m_replayTimelineViewAction->blockSignals(prior);
    }

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("replay/controlsVisible", true);
    settings.setValue("replay/timelineVisible", true);

    SetTimelineDockMode(m_playbackTimeline, false);
    UpdateReplayDockHeightLock();
}

void MainWindow::UpdateReplayDockHeightLock()
{
    if (m_replayControlsDock == nullptr || m_replayTimelineDock == nullptr)
    {
        return;
    }

    const bool controlsDocked = !m_replayControlsDock->isFloating();
    const bool timelineDocked = !m_replayTimelineDock->isFloating();
    if (controlsDocked && timelineDocked)
    {
        QList<QDockWidget*> bottomReplayDocks;
        bottomReplayDocks << m_replayControlsDock << m_replayTimelineDock;
        QList<int> bottomSizes;
        bottomSizes << 44 << 86;
        resizeDocks(bottomReplayDocks, bottomSizes, Qt::Vertical);
    }
}

void MainWindow::OnControlBoolEdited(const QString& key, bool value)
{
    RememberControlValueIfAllowed(key, static_cast<int>(sd::direct::ValueType::Bool), QVariant(value), true);

    // Ian: Track for plugin reconnect republishing (see RepublishPluginControlEdits).
    if (m_connectionConfig.kind == sd::transport::TransportKind::Plugin)
    {
        PluginControlEdit edit;
        edit.valueType = static_cast<int>(sd::direct::ValueType::Bool);
        edit.value = QVariant(value);
        m_pluginControlEdits[key.toStdString()] = edit;
    }

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

    // Ian: Multi-line plot cleanup on removal.
    // If this tile is a multi-line plot owner, remove all its absorbed keys
    // from the reverse map (the disband signal will handle re-showing tiles).
    // If this tile's key is absorbed by another plot, remove it from that
    // plot's source list and from the reverse map.
    if (it->second != nullptr)
    {
        if (it->second->IsMultiLinePlot())
        {
            const auto sources = it->second->GetPlotSources();
            for (const auto& src : sources)
            {
                m_plotSourceOwners.erase(src.key.toStdString());
            }
        }
        m_plotSourceOwners.erase(keyStd);

        m_selectedTiles.remove(it->second);
        it->second->deleteLater();
    }

    m_tiles.erase(it);
    m_savedLayoutByKey.erase(keyStd);
    MarkLayoutDirty();

    // Ian: Notify observers (Run Browser dock) that a tile was removed from
    // the layout.  The dock removes the corresponding tree node so the tree
    // stays a 1:1 mirror of what tiles exist.
    emit TileRemoved(key);
}

// Ian: Called when the user right-clicks a tile and selects "Hide."
// We delegate to the Run Browser dock which unchecks the corresponding
// leaf, recomputes tri-states, and emits CheckedSignalsChanged.
// The standard OnRunBrowserCheckedSignalsChanged handler then hides the
// tile.  This keeps the Run Browser as the single source of truth for
// tile visibility.
//
// If the Run Browser is not active, we just hide the tile directly
// (there is no tree to uncheck).  The tile will reappear on next
// transport start since m_runBrowserActive will be false and all tiles
// are shown.
void MainWindow::OnHideTileRequested(const QString& key)
{
    if (m_runBrowserDock != nullptr && m_runBrowserActive)
    {
        m_runBrowserDock->UncheckSignalByKey(key);
        // CheckedSignalsChanged emission from the dock will call
        // OnRunBrowserCheckedSignalsChanged, which hides the tile.
    }
    else
    {
        // No active Run Browser session — hide directly.
        const std::string keyStd = key.toStdString();
        auto it = m_tiles.find(keyStd);
        if (it != m_tiles.end() && it->second != nullptr)
        {
            it->second->hide();
        }
    }
}

// Ian: Called when the user right-clicks a selected tile in multi-select and
// chooses "Hide N tiles."  Collects keys from all selected tiles, delegates
// to the batch UncheckSignalsByKeys (single signal emission), then clears
// the selection.  When the Run Browser is not active, falls back to hiding
// tiles directly.
void MainWindow::HideSelectedTiles()
{
    if (m_selectedTiles.isEmpty())
    {
        return;
    }

    // Collect keys from the selection before clearing it.
    QSet<QString> keysToHide;
    for (auto* tile : m_selectedTiles)
    {
        if (tile != nullptr)
        {
            keysToHide.insert(tile->GetKey());
        }
    }

    ClearTileSelection();

    if (m_runBrowserDock != nullptr && m_runBrowserActive)
    {
        m_runBrowserDock->UncheckSignalsByKeys(keysToHide);
        // Single CheckedSignalsChanged emission from the dock will call
        // OnRunBrowserCheckedSignalsChanged, which hides the tiles.
    }
    else
    {
        // No active Run Browser session — hide directly.
        for (const QString& key : keysToHide)
        {
            const std::string keyStd = key.toStdString();
            auto it = m_tiles.find(keyStd);
            if (it != m_tiles.end() && it->second != nullptr)
            {
                it->second->hide();
            }
        }
    }
}

// Ian: Called when the user checks/unchecks groups in the Run Browser dock.
// We show tiles whose keys are in the checked set and hide tiles whose keys
// are not.  The tiles themselves are created by the replay transport pipeline
// (via OnVariableUpdateReceived -> GetOrCreateTile) — the Run Browser only
// controls their visibility.  We also persist the checked state immediately
// so that the user sees the same configuration on next launch.
//
// Important: an empty checkedKeys set does NOT mean "show all."  It means
// "nothing is checked" — which should hide all tiles when the Run Browser
// has a replay loaded (m_runBrowserActive).  When m_runBrowserActive is
// false (no replay loaded), we still respect m_runBrowserHiddenKeys so that
// layout-persisted hidden state is never overridden by transport lifecycle
// events (connect, disconnect, reconnect, ClearDiscoveredKeys, etc.).
//
// Ian: SOVEREIGNTY RULE — only explicit user action (unchecking in the Run
// Browser, right-click Hide, loading a different layout) can change what is
// hidden.  No transport lifecycle event should override hidden state.
void MainWindow::OnRunBrowserCheckedSignalsChanged(const QSet<QString>& checkedKeys, const QMap<QString, QString>& /*keyToType*/)
{
    m_runBrowserCheckedKeys = checkedKeys;

    // Walk all existing tiles and show/hide based on the checked set.
    for (auto& [keyStd, tile] : m_tiles)
    {
        if (tile == nullptr)
        {
            continue;
        }

        // Ian: Plot-absorbed tiles must stay hidden regardless of Run Browser
        // checked state.  Their visibility is managed exclusively by the
        // merge/disband lifecycle in MergeTileIntoPlot / OnPlotSourceDisbanded.
        // Without this guard, CheckedSignalsChanged emissions during transport
        // lifecycle (connect, reconnect, replay load) would re-show absorbed
        // tiles, breaking the multi-line plot visual.
        //
        // Direction B of bidirectional sync: when the user unchecks an absorbed
        // key in the Run Browser, hide the corresponding series line in the
        // owning plot (and vice versa for re-checking).  The tile itself stays
        // hidden — only the series rendering is affected.
        auto ownerIt = m_plotSourceOwners.find(keyStd);
        if (ownerIt != m_plotSourceOwners.end())
        {
            tile->hide();
            // Sync series visibility with Run Browser check state
            if (ownerIt->second != nullptr)
            {
                const QString key = QString::fromStdString(keyStd);
                const bool shouldBeVisible = checkedKeys.contains(key);
                ownerIt->second->SetSeriesVisibleBySource(key, shouldBeVisible);
            }
            continue;
        }

        if (!m_runBrowserActive)
        {
            // Ian: No active Run Browser session.  Respect the layout's
            // hidden keys — tiles whose keys are in m_runBrowserHiddenKeys
            // must stay hidden.  This path fires during StartTransport()
            // when m_runBrowserActive is temporarily false and the dock
            // emits CheckedSignalsChanged as it rebuilds.  Without this
            // guard, every emission would show all tiles, wiping the
            // layout's hidden state.
            const QString key = QString::fromStdString(keyStd);
            if (m_runBrowserHiddenKeys.contains(key))
            {
                tile->hide();
            }
            else
            {
                tile->show();
            }
        }
        else if (checkedKeys.isEmpty())
        {
            // Run Browser is active but nothing checked — hide all.
            tile->hide();
        }
        else
        {
            const QString key = QString::fromStdString(keyStd);
            if (checkedKeys.contains(key))
            {
                tile->show();
            }
            else
            {
                tile->hide();
            }
        }
    }

    // Ian: Keep the in-memory hidden-keys cache in sync with the dock's
    // live state.  m_runBrowserHiddenKeys is the value StartTransport()
    // passes to SetHiddenDiscoveredKeys() on reconnect.  Without this
    // update, any tiles hidden AFTER startup would revert to visible on
    // the next auto-reconnect cycle because m_runBrowserHiddenKeys still
    // held the stale value loaded from QSettings at startup.
    if (m_runBrowserActive
        && m_runBrowserDock != nullptr
        && m_runBrowserDock->IsStreamingModeForTesting())
    {
        m_runBrowserHiddenKeys = m_runBrowserDock->GetHiddenDiscoveredKeys();
    }

    // Persist checked keys so the next launch restores the same state.
    PersistRunBrowserState();
}

void MainWindow::OnControlDoubleEdited(const QString& key, double value)
{
    RememberControlValueIfAllowed(key, static_cast<int>(sd::direct::ValueType::Double), QVariant(value), true);

    // Ian: Track for plugin reconnect republishing (see RepublishPluginControlEdits).
    if (m_connectionConfig.kind == sd::transport::TransportKind::Plugin)
    {
        PluginControlEdit edit;
        edit.valueType = static_cast<int>(sd::direct::ValueType::Double);
        edit.value = QVariant(value);
        m_pluginControlEdits[key.toStdString()] = edit;
    }

    if (m_transport)
    {
        m_transport->PublishDouble(key, value);
    }
}

void MainWindow::OnControlStringEdited(const QString& key, const QString& value)
{
    RememberControlValueIfAllowed(key, static_cast<int>(sd::direct::ValueType::String), QVariant(value), true);

    // Ian: Track for plugin reconnect republishing (see RepublishPluginControlEdits).
    if (m_connectionConfig.kind == sd::transport::TransportKind::Plugin)
    {
        PluginControlEdit edit;
        edit.valueType = static_cast<int>(sd::direct::ValueType::String);
        edit.value = QVariant(value);
        m_pluginControlEdits[key.toStdString()] = edit;
    }

    if (m_transport)
    {
        const auto chooserIt = m_tiles.find(key.toStdString());
        if (chooserIt != m_tiles.end() && chooserIt->second != nullptr && chooserIt->second->GetWidgetType() == "string.chooser")
        {
            m_transport->PublishString(key + "/selected", value);
            return;
        }

        m_transport->PublishString(key, value);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Ian: Multi-select lasso on canvas.  When the user presses on empty
    // canvas space and drags, a QRubberBand draws the selection rectangle.
    // On release, tiles whose geometry intersects the rect join the selection.
    // A plain click (no drag) on empty space clears the selection.
    //
    // Ian: The tiles' mousePressEvent calls QFrame::mousePressEvent which
    // does event->ignore(), so the press propagates up to the canvas even
    // when the click landed on a tile.  We must check childAt() to confirm
    // the press is truly on empty space, not on a tile or its children.
    if (watched == m_canvas && m_isEditable && event != nullptr)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            // Only start lasso if the click is on genuinely empty canvas space.
            // childAt() returns the deepest child widget at the position; if
            // non-null, the click landed on a tile (or one of its children).
            // Ian: The tiles' mousePressEvent calls QFrame::mousePressEvent
            // which does event->ignore(), so the press propagates up to the
            // canvas even when the click landed on a tile.  The childAt()
            // guard prevents the lasso from hijacking those propagated events.
            if (me->button() == Qt::LeftButton && m_canvas->childAt(me->pos()) == nullptr)
            {
                m_lassoOrigin = me->pos();
                m_lassoActive = false;

                if (m_lassoRubberBand == nullptr)
                {
                    m_lassoRubberBand = new QRubberBand(QRubberBand::Rectangle, m_canvas);
                }

                m_lassoRubberBand->setGeometry(QRect(m_lassoOrigin, QSize()));
                m_lassoRubberBand->show();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton) && m_lassoRubberBand != nullptr && m_lassoRubberBand->isVisible())
            {
                m_lassoRubberBand->setGeometry(QRect(m_lassoOrigin, me->pos()).normalized());
                m_lassoActive = true;
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_lassoRubberBand != nullptr && m_lassoRubberBand->isVisible())
            {
                m_lassoRubberBand->hide();

                if (m_lassoActive)
                {
                    const QRect selRect = QRect(m_lassoOrigin, me->pos()).normalized();
                    if (!(me->modifiers() & Qt::ControlModifier))
                    {
                        ClearTileSelection();
                    }
                    SelectTilesInRect(selRect);
                }
                else
                {
                    // Plain click on empty space — clear selection.
                    ClearTileSelection();
                }

                m_lassoActive = false;
                return true;
            }
        }
    }

    // Ian: Group drag coordination.  When a selected tile emits a MouseMove
    // with left button held, and a group drag is active, move all selected
    // tiles by the same delta.  The anchor tile moves itself via its own
    // mouseMoveEvent; we move the others here.
    if (watched != nullptr && event != nullptr && m_isEditable)
    {
        auto* tile = qobject_cast<sd::widgets::VariableTile*>(watched);
        if (tile != nullptr)
        {
            if (!m_suppressLayoutDirty)
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

            if (event->type() == QEvent::MouseButtonPress)
            {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    const bool ctrlHeld = (me->modifiers() & Qt::ControlModifier);

                    if (ctrlHeld)
                    {
                        // Ctrl+click: toggle this tile's selection.
                        tile->SetSelected(!tile->IsSelected());
                        if (tile->IsSelected())
                        {
                            m_selectedTiles.insert(tile);
                        }
                        else
                        {
                            m_selectedTiles.remove(tile);
                        }
                    }
                    else if (!tile->IsSelected())
                    {
                        // Click on unselected tile without Ctrl: clear and select just this one.
                        ClearTileSelection();
                    }

                    // If tile is selected and part of a group, prepare group drag.
                    if (tile->IsSelected() && m_selectedTiles.size() > 1)
                    {
                        BeginGroupDrag(tile, me->globalPosition().toPoint());
                    }
                }
            }
            else if (event->type() == QEvent::MouseButtonRelease)
            {
                if (m_groupDragActive)
                {
                    EndGroupDrag();
                }

                // Ian: Drag-to-merge detection.  When a double-type tile is
                // dropped and its bounding rect intersects a line plot tile
                // that has multi-line drop target mode enabled, merge it into
                // that plot as an additional series.  Requires explicit opt-in
                // via the context menu toggle (Fix 1) to prevent accidental
                // merges.  Any double-type tile can drop in, including other
                // line plots (Fix 3).
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton
                    && tile->GetType() == sd::widgets::VariableType::Double
                    && tile->isVisible())
                {
                    const QRect tileRect = tile->geometry();
                    for (const auto& [otherKeyStd, otherTile] : m_tiles)
                    {
                        if (otherTile == nullptr || otherTile == tile)
                        {
                            continue;
                        }
                        if (!otherTile->IsMultiLinePlotDropTarget() || !otherTile->isVisible())
                        {
                            continue;
                        }
                        // Check if at least 30% of the dropped tile overlaps
                        // the target plot tile (prevents accidental merges
                        // from barely touching edges).
                        const QRect plotRect = otherTile->geometry();
                        const QRect intersection = tileRect.intersected(plotRect);
                        if (intersection.isEmpty())
                        {
                            continue;
                        }
                        const int tileArea = tileRect.width() * tileRect.height();
                        const int overlapArea = intersection.width() * intersection.height();
                        if (tileArea > 0 && overlapArea >= tileArea * 3 / 10)
                        {
                            MergeTileIntoPlot(otherTile, tile);
                            break;  // Only merge into one plot
                        }
                    }
                }
            }
            else if (event->type() == QEvent::Move && m_groupDragActive && tile == m_groupDragAnchor)
            {
                // The anchor tile just moved itself via its own mouseMoveEvent.
                // Apply the same delta to all sibling tiles.
                UpdateGroupDrag(QCursor::pos());
            }
            // Ian: Multi-select context menu.  When the user right-clicks a
            // tile that is part of a multi-selection (>1 tile), show a context
            // menu with "Hide N tiles" instead of the single-tile menu.  We
            // intercept ContextMenu here so it never reaches the tile's own
            // contextMenuEvent.
            else if (event->type() == QEvent::ContextMenu
                     && tile->IsSelected()
                     && m_selectedTiles.size() > 1)
            {
                auto* ce = static_cast<QContextMenuEvent*>(event);
                QMenu menu;
                const int count = m_selectedTiles.size();
                QAction* hideAction = menu.addAction(
                    QString("Hide %1 tiles").arg(count));
                connect(hideAction, &QAction::triggered, this, &MainWindow::HideSelectedTiles);
                menu.exec(ce->globalPos());
                return true;
            }
        }
    }

    // Non-editable tile dirty tracking (fallback for the old path that
    // only ran when !m_suppressLayoutDirty; now handled in the block above).
    if (watched != nullptr && event != nullptr && !m_suppressLayoutDirty && !m_isEditable)
    {
        auto* tile = qobject_cast<sd::widgets::VariableTile*>(watched);
        if (tile != nullptr)
        {
            if (event->type() == QEvent::DynamicPropertyChange)
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
    // Ian: Snapshot current tile visibility state into the layout file.
    // We walk actual tile visibility rather than querying mode-specific state
    // (checked keys, hidden keys) so the saved set is always ground truth
    // regardless of whether we're in streaming, reading, or no-browser mode.
    //
    // IMPORTANT: Exclude tiles hidden because they were absorbed into a
    // multi-line plot.  Absorption is already persisted via linePlotSources
    // on the owning tile — writing absorbed keys into hiddenKeys would
    // double-persist the hidden state and block OnPlotSourceDisbanded from
    // restoring visibility after layout reload.
    QSet<QString> hiddenKeys;
    for (const auto& [keyStd, tile] : m_tiles)
    {
        if (tile != nullptr && tile->isHidden())
        {
            if (m_plotSourceOwners.find(keyStd) != m_plotSourceOwners.end())
            {
                continue;  // Absorbed — hidden state persisted via linePlotSources
            }
            hiddenKeys.insert(QString::fromStdString(keyStd));
        }
    }

    const bool saved = sd::layout::SaveLayout(m_canvas, path, hiddenKeys);
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
    QSet<QString> layoutHiddenKeys;
    if (!sd::layout::LoadLayoutEntries(path, entries, &layoutHiddenKeys))
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
        // Ian: Update the hidden keys set BEFORE creating tiles so that
        // GetOrCreateTile respects the layout's visibility state for newly
        // created tiles (e.g. when the Run Browser is not active and tiles
        // start hidden based on m_runBrowserHiddenKeys).
        m_runBrowserHiddenKeys = layoutHiddenKeys;

        for (const sd::layout::WidgetLayoutEntry& entry : entries)
        {
            const sd::widgets::VariableType inferredType = ToVariableTypeFromWidgetType(entry.widgetType);
            sd::widgets::VariableTile* tile = GetOrCreateTile(entry.variableKey, inferredType);
            ApplyLayoutEntryToTile(tile, entry);
        }

        ApplyTemporaryDefaultValuesToTiles();

        // Ian: Rebuild the reverse map for multi-line plot fan-out routing
        // after all tiles have been created and their linePlotSources restored.
        // Then hide any tiles whose keys are now absorbed into a plot.
        RebuildPlotSourceOwners();
        for (const auto& [absorbedKeyStd, ownerTile] : m_plotSourceOwners)
        {
            auto absorbedIt = m_tiles.find(absorbedKeyStd);
            if (absorbedIt != m_tiles.end() && absorbedIt->second != nullptr)
            {
                absorbedIt->second->hide();
            }
        }

        // Ian: Apply layout-persisted visibility state.
        //
        // When the Run Browser is active (reading/streaming), route through
        // the dock so its checkboxes stay consistent — UncheckSignalsByKeys
        // emits CheckedSignalsChanged, which re-walks all tiles.
        //
        // When the Run Browser is NOT active (e.g. Direct transport, no replay
        // loaded), walk tiles directly.  This handles both newly created tiles
        // and pre-existing tiles whose visibility must be updated when
        // switching between layout files.
        //
        // IMPORTANT: m_runBrowserDock is NEVER null (it's created during
        // MainWindow construction), so we must NOT gate on its pointer.
        // Instead, use m_runBrowserActive to decide whether to route through
        // the dock or walk tiles directly.
        if (m_runBrowserActive && !layoutHiddenKeys.isEmpty())
        {
            m_runBrowserDock->UncheckSignalsByKeys(layoutHiddenKeys);
        }
        else if (!m_runBrowserActive)
        {
            for (auto& [keyStd, tile] : m_tiles)
            {
                if (tile == nullptr)
                {
                    continue;
                }
                const QString key = QString::fromStdString(keyStd);
                if (layoutHiddenKeys.contains(key))
                {
                    tile->hide();
                }
                else
                {
                    tile->show();
                }
            }
        }

        PersistRunBrowserState();
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
    const QString transportName = GetSelectedTransportDisplayName();
    QString connectionText = "Disconnected";
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        const QString replayFile = m_connectionConfig.replayFilePath.trimmed();
        const QString replayName = replayFile.isEmpty() ? "no file selected" : QFileInfo(replayFile).fileName();
        connectionText = QString("%1 (%2)").arg(transportName, replayName);
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Connecting))
    {
        connectionText = QString("%1 - Connecting").arg(transportName);
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Connected))
    {
        connectionText = QString("%1 - Connected").arg(transportName);
    }
    else if (m_connectionState == static_cast<int>(sd::transport::ConnectionState::Stale))
    {
        connectionText = QString("%1 - Stale").arg(transportName);
    }
    else
    {
        connectionText = QString("%1 - Disconnected").arg(transportName);
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

    // Ian: User explicitly clicked Connect — clear the manual-disconnect
    // flag so host-level auto-reconnect is eligible again.
    m_userDisconnected = false;
    StartTransport();
}

void MainWindow::OnDisconnectTransport()
{
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        return;
    }

    // Ian: User explicitly clicked Disconnect — suppress host-level
    // auto-reconnect until the user clicks Connect again.
    m_userDisconnected = true;
    m_reconnectTimer->stop();

    StopTransport();
    // Ian: Route through OnConnectionStateChanged (not UpdateWindowConnectionText directly)
    // so the full state-change pipeline fires: title bar, menu enable/disable, recording event.
    // Calling UpdateWindowConnectionText directly was the original bug — it updated the title
    // but left m_connectionState and the Connect/Disconnect menu items out of sync.
    OnConnectionStateChanged(static_cast<int>(sd::transport::ConnectionState::Disconnected));
}

#ifdef _DEBUG
void MainWindow::OnDebugCommandReceived()
{
    while (m_debugCommandServer && m_debugCommandServer->hasPendingConnections())
    {
        QLocalSocket* socket = m_debugCommandServer->nextPendingConnection();
        // Read the full command (wait briefly for data to arrive)
        if (!socket->waitForReadyRead(500))
        {
            socket->deleteLater();
            continue;
        }
        // Ian: Keep the original (case-sensitive) string for publish commands
        // where key and value casing matters, but use a lowered copy for
        // command dispatch so "Connect" / "DISCONNECT" etc. still work.
        const QString rawCmd = QString::fromUtf8(socket->readAll()).trimmed();
        const QString cmd = rawCmd.toLower();
        DebugLogUiEvent(QString("debug_cmd_received cmd=%1").arg(rawCmd));
        socket->write("ok\n");
        socket->flush();
        socket->deleteLater();

        if (cmd == "disconnect")
        {
            if (m_connectionConfig.kind != sd::transport::TransportKind::Replay)
            {
                m_userDisconnected = true;
                m_reconnectTimer->stop();
                StopTransport();
                OnConnectionStateChanged(
                    static_cast<int>(sd::transport::ConnectionState::Disconnected));
            }
        }
        else if (cmd == "connect")
        {
            m_userDisconnected = false;
            StartTransport();
        }
        // Ian: "publish double <key> <value>" and "publish string <key> <value>"
        // allow test automation to write values back through the transport without
        // requiring manual UI interaction. Uses the raw (case-sensitive) command
        // so key names and string values preserve their original casing.
        else if (cmd.startsWith("publish ") && m_transport)
        {
            // Parse: "publish <type> <key> <value...>"
            // Split on the raw string to preserve casing.
            const int typeStart = rawCmd.indexOf(' ') + 1;
            const int keyStart = rawCmd.indexOf(' ', typeStart) + 1;
            const int valStart = rawCmd.indexOf(' ', keyStart) + 1;
            if (typeStart > 0 && keyStart > typeStart && valStart > keyStart)
            {
                const QString type = rawCmd.mid(typeStart, keyStart - typeStart - 1).toLower();
                const QString key = rawCmd.mid(keyStart, valStart - keyStart - 1);
                const QString val = rawCmd.mid(valStart);
                if (type == "double")
                {
                    bool ok = false;
                    const double d = val.toDouble(&ok);
                    if (ok)
                    {
                        m_transport->PublishDouble(key, d);
                        DebugLogUiEvent(QString("debug_publish_double key=%1 value=%2").arg(key).arg(d));
                    }
                }
                else if (type == "string")
                {
                    m_transport->PublishString(key, val);
                    DebugLogUiEvent(QString("debug_publish_string key=%1 value=%2").arg(key, val));
                }
                else if (type == "bool")
                {
                    const bool b = (val == "1" || val.toLower() == "true");
                    m_transport->PublishBool(key, b);
                    DebugLogUiEvent(QString("debug_publish_bool key=%1 value=%2").arg(key).arg(b));
                }
            }
        }
    }
}
#endif

void MainWindow::OnUseDirectTransport()
{
    SelectTransport("direct");
}

void MainWindow::OnUseReplayTransport()
{
    if (!m_telemetryFeatureEnabled)
    {
        return;
    }

    SelectTransport("replay");

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
            SelectTransport("direct");

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

void MainWindow::OnEditTransportSettings()
{
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    if (descriptor == nullptr || !descriptor->HasConnectionFields())
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("%1 Settings").arg(descriptor->displayName));

    auto* layout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();
    layout->addLayout(formLayout);

    std::vector<std::pair<QString, QWidget*>> editors;
    editors.reserve(descriptor->connectionFields.size());

    for (const sd::transport::ConnectionFieldDescriptor& field : descriptor->connectionFields)
    {
        QWidget* editor = nullptr;
        const QVariant currentValue = GetConnectionFieldValue(field);

        switch (field.type)
        {
            case sd::transport::ConnectionFieldType::Bool:
            {
                auto* checkBox = new QCheckBox(&dialog);
                checkBox->setChecked(currentValue.toBool());
                editor = checkBox;
                break;
            }
            case sd::transport::ConnectionFieldType::Int:
            {
                auto* spinBox = new QSpinBox(&dialog);
                spinBox->setMinimum(field.intMinimum);
                spinBox->setMaximum(field.intMaximum);
                spinBox->setValue(currentValue.toInt());
                editor = spinBox;
                break;
            }
            case sd::transport::ConnectionFieldType::String:
            default:
            {
                auto* lineEdit = new QLineEdit(currentValue.toString(), &dialog);
                editor = lineEdit;
                break;
            }
        }

        if (editor == nullptr)
        {
            continue;
        }

        if (!field.helpText.trimmed().isEmpty())
        {
            editor->setToolTip(field.helpText);
        }

        formLayout->addRow(field.label + ':', editor);
        editors.push_back({field.id, editor});
    }

#ifdef _DEBUG
    QComboBox* nativeLinkCarrierCombo = nullptr;
    if (ShouldShowNativeLinkCarrierDebugOptions())
    {
        auto* debugGroup = new QGroupBox("Debug Carrier Override", &dialog);
        auto* debugFormLayout = new QFormLayout(debugGroup);
        nativeLinkCarrierCombo = new QComboBox(debugGroup);
        nativeLinkCarrierCombo->addItem("Shared Memory (SHM)", "shm");
        nativeLinkCarrierCombo->addItem("TCP/IP", "tcp");
        const QString currentCarrier = GetNativeLinkCarrierSetting();
        const int index = nativeLinkCarrierCombo->findData(currentCarrier);
        nativeLinkCarrierCombo->setCurrentIndex(index >= 0 ? index : 0);
        nativeLinkCarrierCombo->setToolTip("Debug-only Native Link carrier override for quickly comparing SHM and TCP.");
        debugFormLayout->addRow("Carrier:", nativeLinkCarrierCombo);
        layout->addWidget(debugGroup);
    }
#endif

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    for (const auto& [fieldId, editor] : editors)
    {
        if (auto* checkBox = qobject_cast<QCheckBox*>(editor))
        {
            SetConnectionFieldValue(fieldId, checkBox->isChecked());
        }
        else if (auto* spinBox = qobject_cast<QSpinBox*>(editor))
        {
            SetConnectionFieldValue(fieldId, spinBox->value());
        }
        else if (auto* lineEdit = qobject_cast<QLineEdit*>(editor))
        {
            SetConnectionFieldValue(fieldId, lineEdit->text().trimmed());
        }
    }

#ifdef _DEBUG
    if (nativeLinkCarrierCombo != nullptr)
    {
        SetNativeLinkCarrierSetting(nativeLinkCarrierCombo->currentData().toString());
    }
#endif

    SyncConnectionConfigToPluginSettingsJson();
    ApplyTransportMenuChecks();
    PersistConnectionSettings();

    // Ian: auto_connect is now a host-level concern — the reconnect timer in
    // MainWindow drives retries, not the plugin.  When the user unchecks
    // auto_connect while the transport is running (or while the host reconnect
    // timer is scheduling retries), we stop the timer and the transport so
    // the title bar settles and the Connect button becomes available for a
    // manual single-shot attempt.
    //
    // For all other setting changes (host, port, carrier) the transport is left
    // running so an operator editing fields mid-match does not get an involuntary
    // disconnect.  A message box informs them to reconnect manually.
    const bool newAutoConnect = IsAutoConnectEnabled();

    if (!newAutoConnect)
    {
        // User disabled auto-connect — stop the host-level retry cycle.
        m_reconnectTimer->stop();
        m_userDisconnected = true;

        if (m_transport != nullptr)
        {
            StopTransport();
            OnConnectionStateChanged(static_cast<int>(sd::transport::ConnectionState::Disconnected));
        }
    }
    else if (m_transport != nullptr)
    {
        // Other settings changed while connected — inform the user to reconnect.
        QMessageBox::information(
            this,
            QStringLiteral("Settings Saved"),
            QStringLiteral("Settings have been saved.\n\nReconnect (Disconnect then Connect) for the new settings to take effect.")
        );
    }
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

    // Ian: New file selected — clear the persisted checked keys so the user
    // starts fresh.  The Run Browser tree will be populated by
    // PopulateRunBrowserFromReplayFile() inside StartTransport().
    m_runBrowserCheckedKeys.clear();
    m_runBrowserExpandedPaths.clear();
    PersistRunBrowserState();

    SelectTransport("replay");
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
    SeekPlaybackToUs(0, true);
    UpdatePlaybackUiState();
}

void MainWindow::OnResetAllLinePlots()
{
    ResetAllLinePlots();
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

    SeekPlaybackToUs(cursorUs);
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

    SeekPlaybackToUs(targetUs);
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

    SeekPlaybackToUs(targetUs);
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
    SeekPlaybackToUs(markerUs);
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
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();

    if (m_useDirectTransportAction != nullptr)
    {
        m_useDirectTransportAction->setChecked(m_connectionConfig.transportId == "direct");
    }

    for (QAction* action : m_pluginTransportActions)
    {
        if (action != nullptr)
        {
            action->setChecked(action->data().toString() == m_connectionConfig.transportId);
        }
    }

    if (m_useReplayTransportAction != nullptr)
    {
        m_useReplayTransportAction->setChecked(m_connectionConfig.transportId == "replay");
    }

    if (m_editTransportSettingsAction != nullptr)
    {
        m_editTransportSettingsAction->setEnabled(!replayMode && descriptor != nullptr && descriptor->HasConnectionFields());
        m_editTransportSettingsAction->setText(
            descriptor != nullptr && descriptor->HasConnectionFields()
                ? QString("%1 Settings...").arg(descriptor->displayName)
                : QStringLiteral("Transport Settings...")
        );
    }

    if (m_useReplayTransportAction != nullptr)
    {
        m_useReplayTransportAction->setEnabled(m_telemetryFeatureEnabled);
    }

    if (m_openReplayFileAction != nullptr)
    {
        m_openReplayFileAction->setEnabled(m_telemetryFeatureEnabled);
    }

    // Ian: Connect is only meaningful when the transport is stopped (Disconnected).
    // Disconnect is only meaningful when the transport is running (Connecting/Connected/Stale).
    // Graying them out in the wrong state avoids confusing double-clicks and makes
    // the menu self-documenting about current transport lifecycle.
    const bool isDisconnected =
        m_connectionState == static_cast<int>(sd::transport::ConnectionState::Disconnected);
    const bool isRunning = !isDisconnected;

    if (m_connectTransportAction != nullptr)
    {
        m_connectTransportAction->setEnabled(!replayMode && isDisconnected);
    }

    if (m_disconnectTransportAction != nullptr)
    {
        m_disconnectTransportAction->setEnabled(!replayMode && isRunning);
    }
}

void MainWindow::PersistConnectionSettings() const
{
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("connection/transportKind", static_cast<int>(m_connectionConfig.kind));
    settings.setValue("connection/transportId", m_connectionConfig.transportId);
    settings.setValue("connection/ntHost", m_connectionConfig.ntHost);
    settings.setValue("connection/ntTeam", m_connectionConfig.ntTeam);
    settings.setValue("connection/ntUseTeam", m_connectionConfig.ntUseTeam);
    settings.setValue("connection/ntClientName", m_connectionConfig.ntClientName);
    settings.setValue("connection/pluginSettingsJson", m_connectionConfig.pluginSettingsJson);
    settings.setValue("connection/replayFilePath", m_connectionConfig.replayFilePath);
    settings.setValue("telemetry/enabled", m_telemetryFeatureEnabled);
    settings.setValue("telemetry/recordEnabled", m_recordRequested);
}

void MainWindow::LoadRememberedControlValues()
{
    m_rememberedControlValues.clear();

#if !SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS
    return;
#endif

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    const int size = settings.beginReadArray("directRememberedControls");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        const QString key = settings.value("key").toString();
        if (key.isEmpty())
        {
            continue;
        }

        RememberedControlValue remembered;
        remembered.valueType = settings.value("valueType", static_cast<int>(sd::direct::ValueType::String)).toInt();
        remembered.value = settings.value("value");
        m_rememberedControlValues[key.toStdString()] = remembered;
    }
    settings.endArray();
}

void MainWindow::SaveRememberedControlValues() const
{
#if !SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS
    return;
#endif

    if (!CurrentTransportUsesRememberedControlValues())
    {
        return;
    }

    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.beginWriteArray("directRememberedControls");
    int index = 0;
    for (const auto& [key, remembered] : m_rememberedControlValues)
    {
        settings.setArrayIndex(index++);
        settings.setValue("key", QString::fromStdString(key));
        settings.setValue("valueType", remembered.valueType);
        settings.setValue("value", remembered.value);
    }
    settings.endArray();
}

void MainWindow::ApplyRememberedControlValuesToTiles()
{
#if !SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS
    return;
#endif

    for (const auto& [keyStd, remembered] : m_rememberedControlValues)
    {
        const auto tileIt = m_tiles.find(keyStd);
        if (tileIt == m_tiles.end() || tileIt->second == nullptr)
        {
            continue;
        }

        sd::widgets::VariableTile* tile = tileIt->second;
        if (remembered.valueType == static_cast<int>(sd::direct::ValueType::Bool)
            && tile->GetType() == sd::widgets::VariableType::Bool)
        {
            tile->SetBoolValue(remembered.value.toBool());
        }
        else if (remembered.valueType == static_cast<int>(sd::direct::ValueType::Double)
                 && tile->GetType() == sd::widgets::VariableType::Double)
        {
            tile->SetDoubleValue(remembered.value.toDouble());
        }
        else if (remembered.valueType == static_cast<int>(sd::direct::ValueType::String)
                 && tile->GetType() == sd::widgets::VariableType::String)
        {
            tile->SetStringValue(remembered.value.toString());
        }
    }
}

void MainWindow::ApplyTemporaryDefaultValuesToTiles()
{
#if !SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS
    return;
#endif

    m_temporaryDefaultValues.clear();
    for (const auto& [keyStd, tile] : m_tiles)
    {
        if (!IsTemporaryDefaultEligibleWidget(tile) || tile->HasValue())
        {
            continue;
        }

        const QVariant defaultValue = TemporaryDefaultValueForTile(tile);
        if (!defaultValue.isValid())
        {
            continue;
        }

        TemporaryDefaultValue remembered;
        const auto type = tile->GetType();
        if (type == sd::widgets::VariableType::Bool)
        {
            remembered.valueType = static_cast<int>(sd::direct::ValueType::Bool);
            remembered.value = defaultValue;
            tile->SetTemporaryDefaultBoolValue(defaultValue.toBool());
        }
        else if (type == sd::widgets::VariableType::Double)
        {
            remembered.valueType = static_cast<int>(sd::direct::ValueType::Double);
            remembered.value = defaultValue;
            tile->SetTemporaryDefaultDoubleValue(defaultValue.toDouble());
        }
        else if (type == sd::widgets::VariableType::String)
        {
            remembered.valueType = static_cast<int>(sd::direct::ValueType::String);
            remembered.value = defaultValue;
            tile->SetTemporaryDefaultStringValue(defaultValue.toString());
        }
        else
        {
            continue;
        }

        m_temporaryDefaultValues[keyStd] = remembered;
    }
}

void MainWindow::RememberControlValueIfAllowed(const QString& key, int valueType, const QVariant& value, bool persistToSettings)
{
#if !SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS
    static_cast<void>(key);
    static_cast<void>(valueType);
    static_cast<void>(value);
    static_cast<void>(persistToSettings);
    return;
#endif

    if (key.isEmpty() || !CurrentTransportUsesRememberedControlValues())
    {
        return;
    }

    // Ian: This helper is the persistence boundary for operator controls.
    // Call it from local edit handlers only. If telemetry/update paths call it,
    // transport-retained state gets reclassified as dashboard-local memory and
    // the next restart looks like a persistence bug instead of retained truth.
    RememberedControlValue remembered;
    remembered.valueType = valueType;
    remembered.value = value;
    m_rememberedControlValues[key.toStdString()] = remembered;

    if (persistToSettings)
    {
        SaveRememberedControlValues();
    }
}

void MainWindow::StartTransport()
{
    StopTransport();
    StartSessionRecording();

    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    if (descriptor != nullptr)
    {
        m_connectionConfig.kind = descriptor->kind;
    }

    m_transport = m_transportRegistry.CreateTransport(m_connectionConfig);
    DebugLogUiEvent(QString("transport_start id=%1 kind=%2")
        .arg(m_connectionConfig.transportId, QString::number(static_cast<int>(m_connectionConfig.kind))));
    if (!m_transport)
    {
        StopSessionRecording();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
        return;
    }

    const bool started = m_transport->Start(
        [this](const sd::transport::VariableUpdate& update)
        {
            if (QThread::currentThread() == thread())
            {
                OnVariableUpdateReceived(update.key, update.valueType, update.value, static_cast<quint64>(update.seq));
                return;
            }

            bool scheduleDrain = false;
            {
                std::lock_guard<std::mutex> lock(m_pendingUiUpdatesMutex);
                m_pendingUiUpdates.push_back(update);
                if (!m_uiDrainScheduled)
                {
                    m_uiDrainScheduled = true;
                    scheduleDrain = true;
                }
            }

            if (scheduleDrain)
            {
                QMetaObject::invokeMethod(this, &MainWindow::DrainPendingUiUpdates, Qt::QueuedConnection);
            }
        },
        [this](sd::transport::ConnectionState state)
        {
            QMetaObject::invokeMethod(this, [this, state]()
            {
                OnConnectionStateChanged(static_cast<int>(state));
            }, Qt::QueuedConnection);
        }
    );

    DebugLogUiEvent(QString("transport_start_result=%1").arg(started ? "ok" : "failed"));

    if (started)
    {
        if (m_transport)
        {
            // Ian: Retained replay uses synthetic seq=0 values on startup.
            // Clear per-key sequence gates first so a dashboard restart accepts
            // those values and visibly repaints tiles instead of treating them
            // as older than the previous live session.
            m_variableStore.ResetSequenceTracking();

            m_transport->ReplayRetainedControls(
                [this](const QString& key, int valueType, const QVariant& value)
                {
                    OnVariableUpdateReceived(key, valueType, value, 0);
                }
            );

            // Ian: Snapshot handling stops here now. Any remembered-control
            // republish still belongs in `OnConnectionStateChanged(Connected)` so
            // all dashboard instances get a full retained snapshot pass before any
            // of them start asserting local operator-owned values back onto the
            // authority. The current default build disables remembered controls,
            // but this ordering rule still matters if that feature is re-enabled.
            if (CurrentTransportUsesRememberedControlValues())
            {
                ApplyRememberedControlValuesToTiles();
            }
            ApplyTemporaryDefaultValuesToTiles();

            // Refresh remembered control cache from current tiles so reconnects can replay
            // the latest operator-facing values even if no new edit event occurs.
            if (CurrentTransportUsesRememberedControlValues())
            {
                for (const auto& [_, tile] : m_tiles)
                {
                    if (!IsOperatorControlWidget(tile))
                    {
                        continue;
                    }

                    const QString key = tile->GetKey();
                    if (key.isEmpty())
                    {
                        continue;
                    }

                    auto rememberedIt = m_rememberedControlValues.find(key.toStdString());
                    if (rememberedIt == m_rememberedControlValues.end())
                    {
                        continue;
                    }

                    // Ian: Refresh only values that were already remembered from
                    // an explicit local edit. Startup retained tiles can carry
                    // authority-owned state, and inventing new remembered entries
                    // here turns transport replay into fake dashboard persistence.
                    RememberedControlValue remembered;
                    const auto type = tile->GetType();
                    if (type == sd::widgets::VariableType::Bool)
                    {
                        remembered.valueType = static_cast<int>(sd::direct::ValueType::Bool);
                        remembered.value = QVariant(tile->GetBoolValue());
                    }
                    else if (type == sd::widgets::VariableType::Double)
                    {
                        remembered.valueType = static_cast<int>(sd::direct::ValueType::Double);
                        remembered.value = QVariant(tile->GetDoubleValue());
                    }
                    else
                    {
                        remembered.valueType = static_cast<int>(sd::direct::ValueType::String);
                        remembered.value = QVariant(tile->GetStringValue());
                    }
                    rememberedIt->second = remembered;
                }
            }
        }

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

        // Ian: If this is a replay transport, populate the Run Browser dock
        // with the replay file's signal hierarchy and restore persisted
        // checked/expanded state.  This covers all replay start paths:
        // OnOpenReplayFile (fresh file), persisted restart, OnUseReplayTransport.
        //
        // For non-replay (layout-mirror) transports, activate the Run Browser
        // in streaming mode.  The tree mirrors whatever tiles exist on the
        // layout.  New tiles arriving from the transport will emit TileAdded
        // (via GetOrCreateTile), which the dock handles via OnTileAdded.
        // We also scan existing tiles (from saved layouts) so they appear in
        // the tree immediately.
        if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
        {
            PopulateRunBrowserFromReplayFile();
        }
        else
        {
            // Ian: Switching to a live transport — set up layout-mirror mode.
            // ClearDiscoveredKeys() resets prior streaming state; it is a
            // no-op when coming from replay mode (the guard inside skips
            // when m_streamingMode is false).  SetStreamingRootLabel() then
            // fully reinitializes streaming mode regardless.  Pre-load
            // the persisted hidden keys so OnTileAdded can lazily apply
            // opt-outs as groups are created.
            //
            // Ian: We must temporarily disable m_runBrowserActive around the
            // clear call.  ClearDiscoveredKeys() emits CheckedSignalsChanged
            // with an empty set, and the OnRunBrowserCheckedSignalsChanged
            // handler interprets "active + empty checked set" as "hide all
            // tiles".  If a previous session left m_runBrowserActive == true
            // (persisted), the empty emission would hide every tile that
            // ReplayRetainedControls just created — and since GetOrCreateTile
            // returns early for existing tiles, they would never be re-shown.
            // Disabling the flag ensures the empty emission is harmless (the
            // handler takes the "not active → show all" path).
            m_runBrowserActive = false;
            if (m_runBrowserDock != nullptr)
            {
                m_runBrowserDock->ClearDiscoveredKeys();
                // Ian: Set the root label before keys arrive so the
                // synthetic root node is named after the transport
                // (e.g. "Direct", "Native Link", "NT4").
                m_runBrowserDock->SetStreamingRootLabel(GetSelectedTransportDisplayName());
                if (!m_runBrowserHiddenKeys.isEmpty())
                {
                    m_runBrowserDock->SetHiddenDiscoveredKeys(m_runBrowserHiddenKeys);
                }

                // Ian: Scan existing tiles (loaded from saved layout or
                // created by ReplayRetainedControls above) and feed them
                // into the dock.  This ensures tiles that existed before
                // the transport started appear in the tree immediately,
                // rather than waiting for the transport to re-deliver keys.
                for (const auto& [tileKeyStd, tile] : m_tiles)
                {
                    if (tile == nullptr)
                    {
                        continue;
                    }
                    QString typeStr;
                    const auto tileType = tile->GetType();
                    switch (tileType)
                    {
                        case sd::widgets::VariableType::Bool:   typeStr = QStringLiteral("bool");   break;
                        case sd::widgets::VariableType::Double:  typeStr = QStringLiteral("double"); break;
                        case sd::widgets::VariableType::String:  typeStr = QStringLiteral("string"); break;
                    }
                    m_runBrowserDock->OnTileAdded(QString::fromStdString(tileKeyStd), typeStr);
                }
            }
            m_runBrowserActive = true;
            PersistRunBrowserState();
        }
    }

    if (!started)
    {
        StopSessionRecording();
        m_transport.reset();
        OnConnectionStateChanged(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    UpdatePlaybackUiState();
}

void MainWindow::StopTransport()
{
    // Ian: Stop the camera stream before tearing down the transport so the
    // dock doesn't keep retrying a connection to a server that's going away.
    if (m_cameraDock != nullptr)
    {
        m_cameraDock->StopStream();
    }

    if (m_transport)
    {
        RecordConnectionEvent(static_cast<int>(sd::transport::ConnectionState::Disconnected));
        m_transport->Stop();
        m_transport.reset();
    }

    StopSessionRecording();
    UpdatePlaybackUiState();
}

// Ian: Host-level auto-connect check.  Reads `auto_connect` from the shared
// plugin_settings_json so the setting works identically for every plugin
// transport (Native Link, NT4, Legacy NT) without each plugin
// having to implement its own retry loop.  Defaults to true when the field
// is absent or the JSON is empty, matching the original per-plugin behavior.
bool MainWindow::IsAutoConnectEnabled() const
{
    if (m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        return true;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
    if (!doc.isObject())
    {
        return true;
    }
    const QJsonValue v = doc.object().value("auto_connect");
    return v.isUndefined() ? true : v.toBool(true);
}

// Ian: Reconnect-timer callback.  Fires once per interval (single-shot) and
// performs a full Stop()+Start() cycle on the transport.  If Start() fails
// synchronously, we fire Disconnected which re-arms the timer for another
// attempt.  If Start() succeeds and the plugin later fires Connected, the
// timer is stopped in OnConnectionStateChanged.  If the plugin fires
// Disconnected asynchronously (connect timeout, server rejected us, etc.),
// OnConnectionStateChanged re-arms the timer again.
//
// The Stop()+Start() pattern is simple and safe: Stop() joins the worker
// thread and resets state, then Start() creates a fresh worker with a
// single connect attempt.  No new plugin API is needed.
void MainWindow::OnReconnectTimerFired()
{
    DebugLogUiEvent(QStringLiteral("reconnect_timer_fired"));

    // Guard: if the user clicked Disconnect while the timer was in flight,
    // or auto-connect was disabled in settings, do nothing.
    if (m_userDisconnected || !IsAutoConnectEnabled())
    {
        return;
    }

    // Guard: replay transport should never auto-reconnect.
    if (m_connectionConfig.kind == sd::transport::TransportKind::Replay)
    {
        return;
    }

    // Perform the stop+start cycle.  StartTransport() internally calls
    // StopTransport() first, so we don't need an explicit Stop() here.
    StartTransport();
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

    UpdateReplayDockHeightLock();

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

            const std::int64_t activeWindowSpanUs =
                std::max<std::int64_t>(1, m_playbackTimeline->GetWindowEndUs() - m_playbackTimeline->GetWindowStartUs());
            if (m_playbackCursorStatusLabel != nullptr)
            {
                m_playbackCursorStatusLabel->setText(QString("t=%1").arg(static_cast<double>(cursorUs) / 1000000.0, 0, 'f', 3));
            }
            if (m_playbackWindowStatusLabel != nullptr)
            {
                m_playbackWindowStatusLabel->setText(
                    QString("window=%1").arg(static_cast<double>(activeWindowSpanUs) / 1000000.0, 0, 'f', 3)
                );
            }
        }
        else
        {
            m_playbackTimeline->SetDurationUs(0);
            m_playbackTimeline->SetCursorUs(0);
            m_playbackTimeline->SetWindowUs(0, 0);
            m_playbackTimeline->SetMarkers({});
            RefreshReplayMarkerList(0);

            if (m_playbackCursorStatusLabel != nullptr)
            {
                m_playbackCursorStatusLabel->setText("t=0.000s");
            }
            if (m_playbackWindowStatusLabel != nullptr)
            {
                m_playbackWindowStatusLabel->setText("window=0.000s");
            }
        }
    }

    if (m_playbackCursorStatusLabel != nullptr)
    {
        m_playbackCursorStatusLabel->setVisible(m_telemetryFeatureEnabled);
    }
    if (m_playbackWindowStatusLabel != nullptr)
    {
        m_playbackWindowStatusLabel->setVisible(m_telemetryFeatureEnabled);
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
    SeekPlaybackToUs(targetUs);
    UpdatePlaybackUiState();
}

void MainWindow::ClearTileSelection()
{
    for (auto* tile : m_selectedTiles)
    {
        if (tile != nullptr)
        {
            tile->SetSelected(false);
        }
    }

    m_selectedTiles.clear();
    m_groupDragActive = false;
    m_groupDragAnchor = nullptr;
    m_groupDragUpdating = false;
    m_groupDragEntries.clear();
}

void MainWindow::SelectTilesInRect(const QRect& selectionRect)
{
    for (auto& [_, tile] : m_tiles)
    {
        if (tile == nullptr || !tile->isVisible())
        {
            continue;
        }

        if (selectionRect.intersects(tile->geometry()))
        {
            tile->SetSelected(true);
            m_selectedTiles.insert(tile);
        }
    }
}

void MainWindow::BeginGroupDrag(sd::widgets::VariableTile* anchorTile, const QPoint& globalPos)
{
    m_groupDragActive = true;
    m_groupDragAnchor = anchorTile;
    m_groupDragUpdating = false;
    m_groupDragEntries.clear();
    m_groupDragEntries.reserve(m_selectedTiles.size());
    for (auto* tile : m_selectedTiles)
    {
        if (tile != nullptr)
        {
            m_groupDragEntries.push_back({ tile, tile->pos() });
        }
    }
}

// Ian: UpdateGroupDrag computes the delta from the anchor tile's current
// position vs. its snapshotted start position, then applies that delta to
// all sibling tiles.  The m_groupDragUpdating guard prevents re-entry:
// moving a sibling fires QEvent::Move on that sibling, which would
// re-enter this function if we didn't suppress it.
void MainWindow::UpdateGroupDrag(const QPoint& globalPos)
{
    if (!m_groupDragActive || m_groupDragEntries.empty() || m_groupDragUpdating)
    {
        return;
    }

    // Find the anchor's start position from the snapshot.
    QPoint anchorStartPos;
    bool foundAnchor = false;
    for (const auto& entry : m_groupDragEntries)
    {
        if (entry.tile == m_groupDragAnchor)
        {
            anchorStartPos = entry.startPos;
            foundAnchor = true;
            break;
        }
    }

    if (!foundAnchor || m_groupDragAnchor == nullptr)
    {
        return;
    }

    const QPoint delta = m_groupDragAnchor->pos() - anchorStartPos;

    // Apply the same delta to all sibling tiles (not the anchor — it already
    // moved itself via its own mouseMoveEvent).
    m_groupDragUpdating = true;
    for (const auto& entry : m_groupDragEntries)
    {
        if (entry.tile == nullptr || entry.tile == m_groupDragAnchor)
        {
            continue;
        }
        entry.tile->move(entry.startPos + delta);
    }
    m_groupDragUpdating = false;
}

void MainWindow::EndGroupDrag()
{
    m_groupDragActive = false;
    m_groupDragAnchor = nullptr;
    m_groupDragUpdating = false;
    m_groupDragEntries.clear();
}

// Ian: Multi-line plot merge.  Called when the user drops a number tile
// onto a line plot tile (drag-to-merge gesture).  The source tile is hidden,
// its key is added as an additional series on the plot tile, and the reverse
// map is updated so OnVariableUpdateReceived can fan-out values.
// Fix 3: When the source tile is itself a line plot (possibly multi-line),
// transfer all its series into the target plot before hiding it.
// Fix 5: Records originalWidgetType so disband can restore it.
void MainWindow::MergeTileIntoPlot(sd::widgets::VariableTile* plotTile, sd::widgets::VariableTile* sourceTile)
{
    if (plotTile == nullptr || sourceTile == nullptr)
    {
        return;
    }

    if (!plotTile->IsLinePlotWidget())
    {
        return;
    }

    const QString sourceKey = sourceTile->GetKey();
    const std::string sourceKeyStd = sourceKey.toStdString();

    // Don't merge a tile into itself
    if (sourceKey == plotTile->GetKey())
    {
        return;
    }

    // Don't merge if already a source
    const auto existingSources = plotTile->GetPlotSources();
    for (const auto& src : existingSources)
    {
        if (src.key == sourceKey)
        {
            return;
        }
    }

    // Ian: If the source tile is itself a multi-line plot, absorb all its
    // secondary series first (Fix 3).  Each transferred series keeps its
    // color and originalWidgetType.  The source tile's own key is added
    // separately below.
    if (sourceTile->IsMultiLinePlot())
    {
        const auto sourceSources = sourceTile->GetPlotSources();
        for (const auto& ss : sourceSources)
        {
            // Skip if already present on the target
            bool alreadyPresent = false;
            const auto currentSources = plotTile->GetPlotSources();
            for (const auto& cs : currentSources)
            {
                if (cs.key == ss.key)
                {
                    alreadyPresent = true;
                    break;
                }
            }
            if (alreadyPresent || ss.key == plotTile->GetKey())
            {
                continue;
            }

            plotTile->AddPlotSource(ss.key, ss.color);
            // Preserve the original widget type from the transferred source
            auto targetSources = plotTile->GetPlotSources();
            for (auto& ts : targetSources)
            {
                if (ts.key == ss.key && ts.originalWidgetType.isEmpty())
                {
                    ts.originalWidgetType = ss.originalWidgetType;
                }
            }
            plotTile->SetPlotSources(targetSources);
            m_plotSourceOwners[ss.key.toStdString()] = plotTile;
        }
        // Disband the source tile's own multi-line (without re-showing tiles)
        // so it becomes a plain tile again before hiding.
        sourceTile->SetPlotSources({});
        sourceTile->SetMultiLinePlotMode(false);
    }

    // Pick next color from palette
    const auto& palette = sd::widgets::LinePlotWidget::DefaultColorPalette();
    const auto updatedSources = plotTile->GetPlotSources();
    const int colorIndex = static_cast<int>(updatedSources.size() + 1) % static_cast<int>(palette.size());
    const QColor color = palette[colorIndex];

    plotTile->AddPlotSource(sourceKey, color);

    // Ian: Record original widget type for disband restoration (Fix 5).
    {
        auto sources = plotTile->GetPlotSources();
        for (auto& src : sources)
        {
            if (src.key == sourceKey && src.originalWidgetType.isEmpty())
            {
                src.originalWidgetType = sourceTile->GetWidgetType();
            }
        }
        plotTile->SetPlotSources(sources);
    }

    m_plotSourceOwners[sourceKeyStd] = plotTile;

    // Hide the source tile — its data now renders inside the plot tile
    sourceTile->hide();
    m_selectedTiles.remove(sourceTile);

    MarkLayoutDirty();
}

// Ian: Multi-line plot disband handler.  Called when a plot tile's widget
// type changes away from line plot, or when the user explicitly removes a
// source from the properties dialog.  Restores the source key's standalone
// tile to visibility (creates it if it was removed).
// Fix 5: Restores originalWidgetType if recorded.
void MainWindow::OnPlotSourceDisbanded(sd::widgets::VariableTile* plotTile, const QString& sourceKey, const QString& originalWidgetType)
{
    Q_UNUSED(plotTile);
    const std::string sourceKeyStd = sourceKey.toStdString();

    // Remove from reverse map
    m_plotSourceOwners.erase(sourceKeyStd);

    // Ian: Remove the key from m_runBrowserHiddenKeys.  When a tile is
    // absorbed into a multi-line plot, it gets hidden via tile->hide().
    // SaveLayoutToPath now correctly excludes absorbed keys from the
    // hiddenKeys set, but older layout files (or a save from before this
    // fix) may still have them.  Without this removal, the "else if
    // (!m_runBrowserHiddenKeys.contains)" check below would block the
    // tile from becoming visible again after disband.
    m_runBrowserHiddenKeys.remove(sourceKey);

    // Show or re-create the standalone tile
    auto tileIt = m_tiles.find(sourceKeyStd);
    if (tileIt != m_tiles.end() && tileIt->second != nullptr)
    {
        // Ian: Restore original widget type if we have a record (Fix 5).
        // If no record was saved (empty string), leave the tile as-is.
        if (!originalWidgetType.isEmpty())
        {
            tileIt->second->SetWidgetType(originalWidgetType);
        }

        // Ian: Respect Run Browser visibility when restoring a disbanded
        // source tile.  If the Run Browser is active and the key is not
        // checked, leave it hidden — the user hasn't opted it back in yet.
        if (m_runBrowserActive)
        {
            if (m_runBrowserCheckedKeys.contains(sourceKey))
            {
                tileIt->second->show();
            }
        }
        else if (!m_runBrowserHiddenKeys.contains(sourceKey))
        {
            tileIt->second->show();
        }
    }

    MarkLayoutDirty();
}

// Ian: Rebuild the entire m_plotSourceOwners reverse map by walking all
// tiles.  Used after layout load when many tiles may have linePlotSources
// that were restored from the saved layout.
void MainWindow::RebuildPlotSourceOwners()
{
    m_plotSourceOwners.clear();
    for (const auto& [keyStd, tile] : m_tiles)
    {
        if (tile == nullptr || !tile->IsMultiLinePlot())
        {
            continue;
        }
        const auto sources = tile->GetPlotSources();
        for (const auto& src : sources)
        {
            m_plotSourceOwners[src.key.toStdString()] = tile;
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr)
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Escape clears the multi-select tile selection.
    if (event->key() == Qt::Key_Escape && m_isEditable && !m_selectedTiles.isEmpty())
    {
        ClearTileSelection();
        event->accept();
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

void MainWindow::ResetAllLinePlots()
{
    for (const auto& [_, tile] : m_tiles)
    {
        if (tile == nullptr || !tile->IsLinePlotWidget())
        {
            continue;
        }

        tile->ResetLinePlotGraph();
    }
}

void MainWindow::SeekPlaybackToUs(std::int64_t targetUs, bool rewindToStart)
{
    if (!m_transport || !m_transport->SupportsPlayback())
    {
        return;
    }

    const std::int64_t clampedTargetUs = std::max<std::int64_t>(0, targetUs);
    const std::int64_t currentUs = std::max<std::int64_t>(0, m_transport->GetPlaybackCursorUs());
    const bool movedBackward = clampedTargetUs < currentUs;
    const bool shouldClear = (rewindToStart && m_clearLinePlotsOnRewind)
        || (movedBackward && m_clearLinePlotsOnBackwardSeek);
    if (shouldClear)
    {
        ResetAllLinePlots();
    }

    m_transport->SeekPlaybackUs(clampedTargetUs);
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
    static_cast<void>(kind);
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    return descriptor != nullptr && descriptor->supportsRecording;
}

void MainWindow::SelectTransport(const QString& transportId)
{
    const sd::transport::TransportDescriptor* descriptor = m_transportRegistry.FindTransport(transportId);
    if (descriptor == nullptr)
    {
        return;
    }

    const bool changed = m_connectionConfig.transportId != descriptor->id;
    if (changed && m_transport != nullptr)
    {
        StopTransport();
        UpdateWindowConnectionText(static_cast<int>(sd::transport::ConnectionState::Disconnected));
    }

    m_connectionConfig.transportId = descriptor->id;
    m_connectionConfig.kind = descriptor->kind;
    if (descriptor->kind == sd::transport::TransportKind::Plugin)
    {
        SyncConnectionConfigFromPluginSettingsJson();
    }

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

const sd::transport::TransportDescriptor* MainWindow::GetSelectedTransportDescriptor() const
{
    return m_transportRegistry.FindTransport(m_connectionConfig.transportId);
}

QString MainWindow::GetSelectedTransportDisplayName() const
{
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    if (descriptor == nullptr || descriptor->displayName.trimmed().isEmpty())
    {
        return QStringLiteral("Unknown");
    }

    return descriptor->displayName;
}

bool MainWindow::CurrentTransportUsesShortDisplayKeys() const
{
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    return descriptor != nullptr && descriptor->useShortDisplayKeys;
}

bool MainWindow::CurrentTransportUsesLegacyNtSettings() const
{
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    if (descriptor == nullptr)
    {
        return false;
    }

    return descriptor->settingsProfileId == "legacy-nt";
}

bool MainWindow::CurrentTransportSupportsChooser() const
{
    const sd::transport::TransportDescriptor* descriptor = GetSelectedTransportDescriptor();
    if (descriptor == nullptr)
    {
        return false;
    }

    return descriptor->GetBoolProperty(QString::fromUtf8(sd::transport::kTransportPropertySupportsChooser), false);
}

bool MainWindow::CurrentTransportUsesRememberedControlValues() const
{
#if !SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS
    return false;
#endif

    return m_connectionConfig.kind == sd::transport::TransportKind::Direct;
}

#ifdef SMARTDASHBOARD_TESTS
void MainWindow::SetTransportSelectionForTesting(const QString& transportId, sd::transport::TransportKind kind)
{
    m_connectionConfig.transportId = transportId;
    m_connectionConfig.kind = kind;
}

void MainWindow::SimulateVariableUpdateForTesting(const QString& key, int valueType, const QVariant& value, quint64 seq)
{
    OnVariableUpdateReceived(key, valueType, value, seq);
}

void MainWindow::SimulateControlDoubleEditForTesting(const QString& key, double value)
{
    OnControlDoubleEdited(key, value);
}

void MainWindow::LoadRememberedControlValuesForTesting()
{
    LoadRememberedControlValues();
}

bool MainWindow::LoadLayoutFromPathForTesting(const QString& path, bool applyToExistingTiles, bool persistAsCurrentPath)
{
    return LoadLayoutFromPath(path, applyToExistingTiles, persistAsCurrentPath);
}

bool MainWindow::SaveLayoutToPathForTesting(const QString& path)
{
    return SaveLayoutToPath(path);
}

void MainWindow::ClearWidgetsForTesting()
{
    OnClearWidgets();
}

int MainWindow::RememberedControlValueCountForTesting() const
{
    return static_cast<int>(m_rememberedControlValues.size());
}

bool MainWindow::HasRememberedControlValueForTesting(const QString& key) const
{
    return m_rememberedControlValues.find(key.toStdString()) != m_rememberedControlValues.end();
}

bool MainWindow::TileHasValueForTesting(const QString& key) const
{
    const auto it = m_tiles.find(key.toStdString());
    return it != m_tiles.end() && it->second != nullptr && it->second->HasValue();
}

bool MainWindow::TileIsTemporaryDefaultForTesting(const QString& key) const
{
    const auto it = m_tiles.find(key.toStdString());
    return it != m_tiles.end() && it->second != nullptr && it->second->IsShowingTemporaryDefault();
}

bool MainWindow::TileIsVisibleForTesting(const QString& key) const
{
    const auto it = m_tiles.find(key.toStdString());
    // Ian: Use !isHidden() rather than isVisible() because isVisible()
    // requires the entire ancestor chain (including the MainWindow) to be
    // shown.  In tests the window is never displayed, so isVisible() would
    // always return false.  isHidden() checks only the widget's own flag.
    return it != m_tiles.end() && it->second != nullptr && !it->second->isHidden();
}

void MainWindow::SetConnectionFieldValueForTesting(const QString& fieldId, const QVariant& value)
{
    SetConnectionFieldValue(fieldId, value);
}

void MainWindow::SyncConnectionConfigToPluginSettingsJsonForTesting()
{
    SyncConnectionConfigToPluginSettingsJson();
}

bool MainWindow::GetConnectionFieldBoolForTesting(const QString& fieldId, bool defaultValue) const
{
    sd::transport::ConnectionFieldDescriptor field;
    field.id = fieldId;
    field.type = sd::transport::ConnectionFieldType::Bool;
    field.defaultValue = defaultValue;
    return GetConnectionFieldValue(field).toBool();
}

// Ian: Replay the exact streaming-mode setup block from StartTransport()
// without creating a real transport.  This simulates what happens during
// an auto-reconnect cycle: the dock is cleared, hidden keys are re-applied,
// and existing tiles are re-fed to the dock.  Callers should first create
// tiles via SimulateVariableUpdateForTesting, hide some via the dock,
// then call this to verify hidden state survives the reconnect.
void MainWindow::SimulateStreamingReconnectForTesting()
{
    m_runBrowserActive = false;
    if (m_runBrowserDock != nullptr)
    {
        m_runBrowserDock->ClearDiscoveredKeys();
        m_runBrowserDock->SetStreamingRootLabel(GetSelectedTransportDisplayName());
        if (!m_runBrowserHiddenKeys.isEmpty())
        {
            m_runBrowserDock->SetHiddenDiscoveredKeys(m_runBrowserHiddenKeys);
        }

        for (const auto& [tileKeyStd, tile] : m_tiles)
        {
            if (tile == nullptr)
            {
                continue;
            }
            QString typeStr;
            const auto tileType = tile->GetType();
            switch (tileType)
            {
                case sd::widgets::VariableType::Bool:   typeStr = QStringLiteral("bool");   break;
                case sd::widgets::VariableType::Double:  typeStr = QStringLiteral("double"); break;
                case sd::widgets::VariableType::String:  typeStr = QStringLiteral("string"); break;
            }
            m_runBrowserDock->OnTileAdded(QString::fromStdString(tileKeyStd), typeStr);
        }
    }
    m_runBrowserActive = true;
    PersistRunBrowserState();
}

sd::widgets::RunBrowserDock* MainWindow::GetRunBrowserDockForTesting() const
{
    return m_runBrowserDock;
}
#endif

QVariant MainWindow::GetConnectionFieldValue(const sd::transport::ConnectionFieldDescriptor& field) const
{
    if (field.id == QString::fromUtf8(sd::transport::kTransportFieldHost))
    {
        return m_connectionConfig.ntHost;
    }
    if (field.id == QString::fromUtf8(sd::transport::kTransportFieldTeamNumber))
    {
        return m_connectionConfig.ntTeam;
    }
    if (field.id == QString::fromUtf8(sd::transport::kTransportFieldUseTeamNumber))
    {
        return m_connectionConfig.ntUseTeam;
    }
    if (field.id == QString::fromUtf8(sd::transport::kTransportFieldClientName))
    {
        return m_connectionConfig.ntClientName;
    }

    if (!m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
        if (document.isObject())
        {
            const QJsonValue value = document.object().value(field.id);
            if (!value.isUndefined())
            {
                return value.toVariant();
            }
        }
    }

    return field.defaultValue;
}

void MainWindow::SetConnectionFieldValue(const QString& fieldId, const QVariant& value)
{
    if (fieldId == QString::fromUtf8(sd::transport::kTransportFieldHost))
    {
        m_connectionConfig.ntHost = value.toString();
        return;
    }
    if (fieldId == QString::fromUtf8(sd::transport::kTransportFieldTeamNumber))
    {
        m_connectionConfig.ntTeam = value.toInt();
        return;
    }
    if (fieldId == QString::fromUtf8(sd::transport::kTransportFieldUseTeamNumber))
    {
        m_connectionConfig.ntUseTeam = value.toBool();
        return;
    }
    if (fieldId == QString::fromUtf8(sd::transport::kTransportFieldClientName))
    {
        m_connectionConfig.ntClientName = value.toString();
        return;
    }

    QJsonObject object;
    if (!m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
        if (document.isObject())
        {
            object = document.object();
        }
    }

    object.insert(fieldId, QJsonValue::fromVariant(value));
    m_connectionConfig.pluginSettingsJson = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void MainWindow::SyncConnectionConfigToPluginSettingsJson()
{
    QJsonObject object;
    object.insert(QString::fromUtf8(sd::transport::kTransportFieldHost), m_connectionConfig.ntHost);
    object.insert(QString::fromUtf8(sd::transport::kTransportFieldTeamNumber), m_connectionConfig.ntTeam);
    object.insert(QString::fromUtf8(sd::transport::kTransportFieldUseTeamNumber), m_connectionConfig.ntUseTeam);
    object.insert(QString::fromUtf8(sd::transport::kTransportFieldClientName), m_connectionConfig.ntClientName);
    if (!m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
        if (document.isObject())
        {
            const QJsonObject existing = document.object();
            if (existing.contains("carrier"))
            {
                object.insert("carrier", existing.value("carrier").toString("tcp"));
            }
            if (existing.contains("channel_id"))
            {
                object.insert("channel_id", existing.value("channel_id").toString("native-link-default"));
            }
            if (existing.contains("port"))
            {
                object.insert("port", existing.value("port").toInt(5810));
            }
            // Ian: "auto_connect" is a plugin-owned Bool field stored only in
            // pluginSettingsJson (it has no dedicated ConnectionConfig member).
            // It must be explicitly round-tripped here or SetConnectionFieldValue
            // writes it into the JSON and then this function immediately overwrites
            // the JSON from scratch, silently discarding the user's choice.
            // The plugin fallback is true, so omitting this key is safe for legacy
            // INI entries — but once the user has explicitly set it to false, it
            // must survive this rebuild.
            if (existing.contains("auto_connect"))
            {
                object.insert("auto_connect", existing.value("auto_connect").toBool(true));
            }
        }
    }
    m_connectionConfig.pluginSettingsJson = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void MainWindow::SyncConnectionConfigFromPluginSettingsJson()
{
    if (m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        SyncConnectionConfigToPluginSettingsJson();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
    if (!document.isObject())
    {
        SyncConnectionConfigToPluginSettingsJson();
        return;
    }

    const QJsonObject object = document.object();
    if (object.contains(QString::fromUtf8(sd::transport::kTransportFieldHost)))
    {
        m_connectionConfig.ntHost = object.value(QString::fromUtf8(sd::transport::kTransportFieldHost)).toString(m_connectionConfig.ntHost);
    }
    if (object.contains(QString::fromUtf8(sd::transport::kTransportFieldTeamNumber)))
    {
        m_connectionConfig.ntTeam = object.value(QString::fromUtf8(sd::transport::kTransportFieldTeamNumber)).toInt(m_connectionConfig.ntTeam);
    }
    if (object.contains(QString::fromUtf8(sd::transport::kTransportFieldUseTeamNumber)))
    {
        m_connectionConfig.ntUseTeam = object.value(QString::fromUtf8(sd::transport::kTransportFieldUseTeamNumber)).toBool(m_connectionConfig.ntUseTeam);
    }
    if (object.contains(QString::fromUtf8(sd::transport::kTransportFieldClientName)))
    {
        m_connectionConfig.ntClientName = object.value(QString::fromUtf8(sd::transport::kTransportFieldClientName)).toString(m_connectionConfig.ntClientName);
    }
}

QString MainWindow::GetNativeLinkCarrierSetting() const
{
    if (!m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
        if (document.isObject())
        {
            const QString carrier = document.object().value("carrier").toString().trimmed().toLower();
            if (carrier == "shm")
            {
                return carrier;
            }
        }
    }

    return "tcp";
}

void MainWindow::SetNativeLinkCarrierSetting(const QString& carrier)
{
    QJsonObject object;
    if (!m_connectionConfig.pluginSettingsJson.trimmed().isEmpty())
    {
        const QJsonDocument document = QJsonDocument::fromJson(m_connectionConfig.pluginSettingsJson.toUtf8());
        if (document.isObject())
        {
            object = document.object();
        }
    }

    object.insert("carrier", carrier.trimmed().compare("shm", Qt::CaseInsensitive) == 0 ? "shm" : "tcp");
    if (!object.contains("channel_id"))
    {
        object.insert("channel_id", "native-link-default");
    }
    if (!object.contains("port"))
    {
        object.insert("port", 5810);
    }
    m_connectionConfig.pluginSettingsJson = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool MainWindow::ShouldShowNativeLinkCarrierDebugOptions() const
{
#ifdef _DEBUG
    return m_connectionConfig.transportId == "native-link";
#else
    return false;
#endif
}
