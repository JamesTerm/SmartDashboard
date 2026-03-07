#include "widgets/variable_tile.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace sd::widgets
{
    namespace
    {
        QString GetDefaultWidgetType(VariableType type)
        {
            switch (type)
            {
                case VariableType::Bool:
                    return "bool.led";
                case VariableType::Double:
                    return "double.numeric";
                case VariableType::String:
                    return "string.text";
                default:
                    return "unknown";
            }
        }
    }

    VariableTile::VariableTile(const QString& key, VariableType type, QWidget* parent)
        : QFrame(parent)
        , m_key(key)
        , m_type(type)
        , m_widgetType(GetDefaultWidgetType(type))
    {
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);
        setAutoFillBackground(true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        m_titleLabel = new QLabel(key, this);
        m_valueLabel = new QLabel(FormatValueText(), this);
        m_valueLabel->setStyleSheet("font-weight: 600;");

        layout->addWidget(m_titleLabel);
        layout->addWidget(m_valueLabel);

        setMinimumSize(180, 64);
    }

    void VariableTile::SetEditable(bool editable)
    {
        m_editable = editable;
        setCursor(m_editable ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    void VariableTile::SetBoolValue(bool value)
    {
        m_boolValue = value;
        m_valueLabel->setText(FormatValueText());
    }

    void VariableTile::SetDoubleValue(double value)
    {
        m_doubleValue = value;
        m_valueLabel->setText(FormatValueText());
    }

    void VariableTile::SetStringValue(const QString& value)
    {
        m_stringValue = value;
        m_valueLabel->setText(FormatValueText());
    }

    QString VariableTile::GetKey() const
    {
        return m_key;
    }

    VariableType VariableTile::GetType() const
    {
        return m_type;
    }

    QString VariableTile::GetWidgetType() const
    {
        return m_widgetType;
    }

    void VariableTile::SetWidgetType(const QString& widgetType)
    {
        m_widgetType = widgetType;
    }

    void VariableTile::mousePressEvent(QMouseEvent* event)
    {
        if (m_editable && event->button() == Qt::LeftButton)
        {
            m_dragOrigin = event->globalPosition().toPoint() - frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
        }

        QFrame::mousePressEvent(event);
    }

    void VariableTile::mouseMoveEvent(QMouseEvent* event)
    {
        if (m_editable && (event->buttons() & Qt::LeftButton))
        {
            move(event->globalPosition().toPoint() - m_dragOrigin);
        }

        QFrame::mouseMoveEvent(event);
    }

    void VariableTile::mouseReleaseEvent(QMouseEvent* event)
    {
        if (m_editable && event->button() == Qt::LeftButton)
        {
            setCursor(Qt::OpenHandCursor);
        }

        QFrame::mouseReleaseEvent(event);
    }

    void VariableTile::contextMenuEvent(QContextMenuEvent* event)
    {
        QMenu menu(this);
        BuildContextMenu(menu);
        menu.exec(event->globalPos());
    }

    void VariableTile::BuildContextMenu(QMenu& menu)
    {
        QMenu* changeToMenu = menu.addMenu("Change to...");

        auto addWidgetAction = [this, changeToMenu](const QString& label, const QString& widgetType)
        {
            QAction* action = changeToMenu->addAction(label);
            connect(action, &QAction::triggered, this, [this, widgetType]()
            {
                SetWidgetType(widgetType);
                emit ChangeWidgetRequested(m_key, widgetType);
            });
        };

        switch (m_type)
        {
            case VariableType::Bool:
                addWidgetAction("LED indicator", "bool.led");
                addWidgetAction("True/False text", "bool.text");
                break;
            case VariableType::Double:
                addWidgetAction("Numeric text", "double.numeric");
                addWidgetAction("Progress bar", "double.progress");
                addWidgetAction("Gauge", "double.gauge");
                break;
            case VariableType::String:
                addWidgetAction("Text label", "string.text");
                addWidgetAction("Multiline", "string.multiline");
                break;
            default:
                break;
        }

        menu.addSeparator();
        menu.addAction("Properties...");
    }

    QString VariableTile::FormatValueText() const
    {
        switch (m_type)
        {
            case VariableType::Bool:
                return m_boolValue ? "True" : "False";
            case VariableType::Double:
                return QString::number(m_doubleValue, 'f', 2);
            case VariableType::String:
                return m_stringValue;
            default:
                return "";
        }
    }
}
