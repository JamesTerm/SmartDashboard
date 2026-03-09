#pragma once

#include "layout/layout_serializer.h"
#include "model/variable_store.h"
#include "transport/direct_subscriber_adapter.h"
#include "widgets/variable_tile.h"

#include <QMainWindow>
#include <QVariant>

#include <string>

#include <cstdint>
#include <unordered_map>

class QAction;
class QLabel;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnToggleEditable();
    void OnToggleSnapToGrid();
    void OnSetMoveMode();
    void OnSetResizeMode();
    void OnSetMoveResizeMode();
    void OnVariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void OnConnectionStateChanged(int state);
    void OnSaveLayout();
    void OnLoadLayout();
    void OnClearWidgets();
    void OnRemoveWidgetRequested(const QString& key);
    void OnControlBoolEdited(const QString& key, bool value);
    void OnControlDoubleEdited(const QString& key, double value);
    void OnControlStringEdited(const QString& key, const QString& value);

private:
    using TileMap = std::unordered_map<std::string, sd::widgets::VariableTile*>;
    using LayoutMap = std::unordered_map<std::string, sd::layout::WidgetLayoutEntry>;

    sd::widgets::VariableTile* GetOrCreateTile(const QString& key, sd::widgets::VariableType type);
    void UpdateWindowConnectionText(int state);
    void LoadWindowGeometry();
    void SaveWindowGeometry() const;

    QWidget* m_canvas = nullptr;
    QLabel* m_statusLabel = nullptr;
    QAction* m_editableAction = nullptr;
    QAction* m_snapToGridAction = nullptr;
    QAction* m_moveModeAction = nullptr;
    QAction* m_resizeModeAction = nullptr;
    QAction* m_moveResizeModeAction = nullptr;
    bool m_isEditable = false;
    bool m_snapToGrid = true;
    sd::widgets::EditInteractionMode m_editInteractionMode = sd::widgets::EditInteractionMode::MoveAndResize;
    int m_nextTileOffset = 0;
    std::uint64_t m_lastTransportSeq = 0;
    TileMap m_tiles;
    LayoutMap m_savedLayoutByKey;
    sd::model::VariableStore m_variableStore;
    DirectSubscriberAdapter m_subscriberAdapter;
    class DirectPublisherAdapter* m_commandPublisher = nullptr;
};
