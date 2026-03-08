#include "widgets/variable_tile.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDial>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>

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

        m_layout = new QGridLayout(this);
        m_layout->setContentsMargins(8, 8, 8, 8);
        m_layout->setHorizontalSpacing(8);
        m_layout->setVerticalSpacing(4);
        m_layout->setColumnStretch(0, 0);
        m_layout->setColumnStretch(1, 1);
        m_layout->setColumnMinimumWidth(0, 90);

        m_titleLabel = new QLabel(key, this);
        m_valueLabel = new QLabel(FormatValueText(), this);
        m_valueLabel->setStyleSheet("font-weight: 600;");

        m_boolLed = new QFrame(this);
        m_boolLed->setFixedSize(14, 14);
        m_boolLed->setVisible(false);

        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setVisible(false);

        m_gauge = new QDial(this);
        m_gauge->setRange(0, 100);
        m_gauge->setNotchesVisible(true);
        m_gauge->setWrapping(false);
        m_gauge->setVisible(false);

        m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
        m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_progressBar, 0, 1, 1, 1);
        m_layout->addWidget(m_gauge, 0, 1, 1, 1);

        setMinimumSize(180, 64);
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetEditable(bool editable)
    {
        m_editable = editable;
        setCursor(m_editable ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    void VariableTile::SetBoolValue(bool value)
    {
        m_boolValue = value;
        UpdateValueDisplay();
    }

    void VariableTile::SetDoubleValue(double value)
    {
        m_doubleValue = value;
        UpdateValueDisplay();
    }

    void VariableTile::SetStringValue(const QString& value)
    {
        m_stringValue = value;
        UpdateValueDisplay();
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
        setProperty("widgetType", m_widgetType);
        UpdateWidgetPresentation();
        UpdateValueDisplay();
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

    void VariableTile::UpdateWidgetPresentation()
    {
        const bool isBoolLed = (m_widgetType == "bool.led");
        const bool isBoolText = (m_widgetType == "bool.text");
        const bool isDoubleNumeric = (m_widgetType == "double.numeric");
        const bool isDoubleProgress = (m_widgetType == "double.progress");
        const bool isDoubleGauge = (m_widgetType == "double.gauge");
        const bool isStringText = (m_widgetType == "string.text");
        const bool isStringMultiline = (m_widgetType == "string.multiline");

        const bool showBoolLed = (m_type == VariableType::Bool && isBoolLed);
        const bool showBoolText = (m_type == VariableType::Bool && isBoolText);
        const bool showDoubleNumeric = (m_type == VariableType::Double && isDoubleNumeric);
        const bool showDoubleProgress = (m_type == VariableType::Double && isDoubleProgress);
        const bool showDoubleGauge = (m_type == VariableType::Double && isDoubleGauge);
        const bool showStringText = (m_type == VariableType::String && isStringText);
        const bool showStringMultiline = (m_type == VariableType::String && isStringMultiline);

        m_boolLed->setVisible(showBoolLed);
        m_progressBar->setVisible(showDoubleProgress);
        m_gauge->setVisible(showDoubleGauge);

        const bool showValueLabel = showBoolText || showDoubleNumeric || showStringText || showStringMultiline;
        m_valueLabel->setVisible(showValueLabel);
        m_titleLabel->setVisible(!showDoubleGauge);

        if (showDoubleGauge)
        {
            m_layout->addWidget(m_gauge, 1, 0, 1, 2, Qt::AlignHCenter | Qt::AlignVCenter);
        }
        else if (showDoubleProgress)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_progressBar, 1, 0, 1, 2);
        }
        else if (showStringMultiline)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_valueLabel, 1, 0, 1, 2);
        }
        else
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
            m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_progressBar, 0, 1, 1, 1);
            m_layout->addWidget(m_gauge, 0, 1, 1, 1);
        }

        if (showStringMultiline)
        {
            m_valueLabel->setWordWrap(true);
            m_valueLabel->setMinimumHeight(56);
        }
        else
        {
            m_valueLabel->setWordWrap(false);
            m_valueLabel->setMinimumHeight(0);
        }
    }

    void VariableTile::UpdateValueDisplay()
    {
        if (m_progressBar->isVisible())
        {
            m_progressBar->setValue(DoubleToPercent(m_doubleValue));
            return;
        }

        if (m_gauge->isVisible())
        {
            m_gauge->setValue(DoubleToPercent(m_doubleValue));
            return;
        }

        if (m_boolLed->isVisible())
        {
            UpdateBoolLedAppearance();
            return;
        }

        m_valueLabel->setText(FormatValueText());
    }

    int VariableTile::DoubleToPercent(double value) const
    {
        double clamped = value;
        if (clamped < -1.0)
        {
            clamped = -1.0;
        }
        if (clamped > 1.0)
        {
            clamped = 1.0;
        }

        return static_cast<int>((clamped + 1.0) * 50.0);
    }

    void VariableTile::UpdateBoolLedAppearance()
    {
        const QString color = m_boolValue ? "#2ecc71" : "#7f8c8d";
        m_boolLed->setStyleSheet(
            QString("background-color: %1; border-radius: 7px; border: 1px solid #4b4b4b;").arg(color)
        );
    }
}
