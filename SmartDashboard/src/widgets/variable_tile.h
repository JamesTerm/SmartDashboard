#pragma once

#include <QFrame>

class QLabel;
class QMenu;

namespace sd::widgets
{
    enum class VariableType
    {
        Bool,
        Double,
        String
    };

    class VariableTile final : public QFrame
    {
        Q_OBJECT

    public:
        explicit VariableTile(const QString& key, VariableType type, QWidget* parent = nullptr);

        void SetEditable(bool editable);
        void SetBoolValue(bool value);
        void SetDoubleValue(double value);
        void SetStringValue(const QString& value);

        QString GetKey() const;
        VariableType GetType() const;
        QString GetWidgetType() const;
        void SetWidgetType(const QString& widgetType);

    signals:
        void ChangeWidgetRequested(const QString& key, const QString& widgetType);

    protected:
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void contextMenuEvent(QContextMenuEvent* event) override;

    private:
        void BuildContextMenu(QMenu& menu);
        QString FormatValueText() const;

        QString m_key;
        VariableType m_type;
        QString m_widgetType;
        bool m_editable = false;
        QPoint m_dragOrigin;
        bool m_boolValue = false;
        double m_doubleValue = 0.0;
        QString m_stringValue;

        QLabel* m_titleLabel = nullptr;
        QLabel* m_valueLabel = nullptr;
    };
}
