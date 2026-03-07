#pragma once

#include "layout/layout_serializer.h"
#include "model/variable_store.h"
#include "transport/direct_subscriber_adapter.h"
#include "widgets/variable_tile.h"

#include <QMainWindow>
#include <QVariant>

#include <string>

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
    void OnVariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void OnConnectionStateChanged(int state);
    void OnSaveLayout();
    void OnLoadLayout();

private:
    using TileMap = std::unordered_map<std::string, sd::widgets::VariableTile*>;
    using LayoutMap = std::unordered_map<std::string, sd::layout::WidgetLayoutEntry>;

    sd::widgets::VariableTile* GetOrCreateTile(const QString& key, sd::widgets::VariableType type);
    void UpdateWindowConnectionText(int state);

    QWidget* m_canvas = nullptr;
    QLabel* m_statusLabel = nullptr;
    QAction* m_editableAction = nullptr;
    bool m_isEditable = false;
    int m_nextTileOffset = 0;
    TileMap m_tiles;
    LayoutMap m_savedLayoutByKey;
    sd::model::VariableStore m_variableStore;
    DirectSubscriberAdapter m_subscriberAdapter;
};
