#include "app/main_window.h"

#include "layout/layout_serializer.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_subscriberAdapter(this)
{
    setWindowTitle("SmartDashboard - Disconnected");
    resize(1200, 800);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName("dashboardCanvas");
    m_canvas->setStyleSheet("background-color: #f0f4f7;");
    setCentralWidget(m_canvas);

    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* saveLayoutAction = fileMenu->addAction("Save Layout");
    QAction* loadLayoutAction = fileMenu->addAction("Load Layout");
    connect(saveLayoutAction, &QAction::triggered, this, &MainWindow::OnSaveLayout);
    connect(loadLayoutAction, &QAction::triggered, this, &MainWindow::OnLoadLayout);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    m_editableAction = viewMenu->addAction("Editable");
    m_editableAction->setCheckable(true);
    connect(m_editableAction, &QAction::triggered, this, &MainWindow::OnToggleEditable);

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

    connect(
        qApp,
        &QCoreApplication::aboutToQuit,
        this,
        &MainWindow::OnSaveLayout
    );

    OnLoadLayout();
    m_subscriberAdapter.Start();
}

MainWindow::~MainWindow()
{
    m_subscriberAdapter.Stop();
}

void MainWindow::OnToggleEditable()
{
    m_isEditable = m_editableAction->isChecked();
    for (auto& [_, tile] : m_tiles)
    {
        tile->SetEditable(m_isEditable);
    }
}

void MainWindow::OnVariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq)
{
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
    UpdateWindowConnectionText(state);
}

void MainWindow::OnSaveLayout()
{
    const QString path = sd::layout::GetDefaultLayoutPath();
    sd::layout::SaveLayout(m_canvas, path);
}

void MainWindow::OnLoadLayout()
{
    const QString path = sd::layout::GetDefaultLayoutPath();
    std::vector<sd::layout::WidgetLayoutEntry> entries;
    if (!sd::layout::LoadLayoutEntries(path, entries))
    {
        return;
    }

    m_savedLayoutByKey.clear();
    for (const sd::layout::WidgetLayoutEntry& entry : entries)
    {
        m_savedLayoutByKey[entry.variableKey.toStdString()] = entry;
    }
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
        [tile](const QString&, const QString& widgetType)
        {
            tile->setProperty("widgetType", widgetType);
        }
    );

    tile->setObjectName(QString("tile_%1").arg(QString::number(m_tiles.size() + 1)));
    tile->SetEditable(m_isEditable);

    auto savedIt = m_savedLayoutByKey.find(keyStd);
    if (savedIt != m_savedLayoutByKey.end())
    {
        const sd::layout::WidgetLayoutEntry& entry = savedIt->second;
        tile->setGeometry(entry.geometry);
        if (!entry.widgetType.isEmpty())
        {
            tile->SetWidgetType(entry.widgetType);
        }
    }
    else
    {
        tile->setGeometry(24 + m_nextTileOffset, 32 + m_nextTileOffset, 220, 84);
        m_nextTileOffset = (m_nextTileOffset + 24) % 200;
    }

    tile->setProperty("variableKey", key);
    tile->setProperty("widgetType", tile->GetWidgetType());
    tile->show();

    m_tiles.emplace(keyStd, tile);
    return tile;
}

void MainWindow::UpdateWindowConnectionText(int state)
{
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
