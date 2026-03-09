#include "app/main_window.h"

#include "layout/layout_serializer.h"
#include "transport/direct_publisher_adapter.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QSettings>
#include <QStatusBar>
#include <QVariant>
#include <QWidget>

namespace
{
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
                entry.progressBarUpperLimit.isValid() ? entry.progressBarUpperLimit.toDouble() : 1.0,
                entry.progressBarTickInterval.isValid() ? entry.progressBarTickInterval.toDouble() : 0.2,
                entry.progressBarShowTickMarks.isValid() ? entry.progressBarShowTickMarks.toBool() : false
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
    , m_subscriberAdapter(this)
{
    setWindowTitle("SmartDashboard - Disconnected");
    resize(1200, 800);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName("dashboardCanvas");
    m_canvas->setAutoFillBackground(true);
    m_canvas->setBackgroundRole(QPalette::Window);
    setCentralWidget(m_canvas);

    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* saveLayoutAction = fileMenu->addAction("Save Layout");
    QAction* loadLayoutAction = fileMenu->addAction("Load Layout");
    QAction* clearWidgetsAction = fileMenu->addAction("Clear Widgets");
    connect(saveLayoutAction, &QAction::triggered, this, &MainWindow::OnSaveLayout);
    connect(loadLayoutAction, &QAction::triggered, this, &MainWindow::OnLoadLayout);
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

    m_statusLabel = new QLabel("State: Disconnected", this);
    statusBar()->addPermanentWidget(m_statusLabel);

    connect(
        &m_subscriberAdapter,
        &DirectSubscriberAdapter::VariableUpdateReceived,
        this,
        &MainWindow::OnVariableUpdateReceived
    );
    connect(
        &m_subscriberAdapter,
        &DirectSubscriberAdapter::ConnectionStateChanged,
        this,
        &MainWindow::OnConnectionStateChanged
    );

    m_commandPublisher = new DirectPublisherAdapter(this);
    m_commandPublisher->Start();

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
    m_subscriberAdapter.Start();
}

MainWindow::~MainWindow()
{
    m_subscriberAdapter.Stop();
    if (m_commandPublisher != nullptr)
    {
        m_commandPublisher->Stop();
    }
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
}

void MainWindow::OnConnectionStateChanged(int state)
{
    const int connected = static_cast<int>(sd::direct::ConnectionState::Connected);
    if (state == connected)
    {
        // Reconnect handling: reset sequence gating when transport re-enters connected state.
        m_variableStore.ResetSequenceTracking();
    }

    UpdateWindowConnectionText(state);
}

void MainWindow::OnSaveLayout()
{
    QString selected = QFileDialog::getSaveFileName(
        this,
        "Save Layout",
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
        QMessageBox::warning(this, "Save Layout", "Failed to save layout.");
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

void MainWindow::UpdateWindowConnectionText(int state)
{
    // Finite-state mapping from transport enum -> UI status text.
    QString stateText = "Disconnected";
    if (state == static_cast<int>(sd::direct::ConnectionState::Connecting))
    {
        stateText = "Connecting";
    }
    else if (state == static_cast<int>(sd::direct::ConnectionState::Connected))
    {
        stateText = "Connected";
    }
    else if (state == static_cast<int>(sd::direct::ConnectionState::Stale))
    {
        stateText = "Stale";
    }

    setWindowTitle(QString("SmartDashboard - %1").arg(stateText));
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

void MainWindow::SaveWindowGeometry() const
{
    // Save persisted main-window geometry/state for next launch.
    QSettings settings("SmartDashboard", "SmartDashboardApp");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
}

void MainWindow::OnControlBoolEdited(const QString& key, bool value)
{
    if (m_commandPublisher != nullptr)
    {
        m_commandPublisher->PublishBool(key, value);
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
    if (m_commandPublisher != nullptr)
    {
        m_commandPublisher->PublishDouble(key, value);
    }
}

void MainWindow::OnControlStringEdited(const QString& key, const QString& value)
{
    if (m_commandPublisher != nullptr)
    {
        m_commandPublisher->PublishString(key, value);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != nullptr && event != nullptr && !m_suppressLayoutDirty)
    {
        auto* tile = qobject_cast<sd::widgets::VariableTile*>(watched);
        if (tile != nullptr)
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
    prompt.setText("Do you wish to save this layout?");
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
        QString selected = QFileDialog::getSaveFileName(
            this,
            "Save Layout",
            GetInitialLayoutPath(),
            "Layout Files (*.json);;All Files (*)"
        );
        if (selected.isEmpty())
        {
            event->ignore();
            return;
        }

        if (QFileInfo(selected).suffix().isEmpty())
        {
            selected += ".json";
        }

        if (!SaveLayoutToPath(selected))
        {
            QMessageBox::warning(this, "Save Layout", "Failed to save layout.");
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
    }

    return saved;
}

bool MainWindow::LoadLayoutFromPath(const QString& path, bool applyToExistingTiles)
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

    m_layoutFilePath = path;
    PersistLastLayoutPath(path);
    m_layoutDirty = false;
    m_suppressLayoutDirty = false;
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
}
