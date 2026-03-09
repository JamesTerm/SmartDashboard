#include "widgets/variable_tile.h"

#include "widgets/tile_control_widget.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDial>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QCheckBox>

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

        double PercentToConfiguredRange(int rawPercent, double lower, double upper)
        {
            double clampedPercent = static_cast<double>(rawPercent);
            if (clampedPercent < 0.0)
            {
                clampedPercent = 0.0;
            }
            if (clampedPercent > 100.0)
            {
                clampedPercent = 100.0;
            }

            const double normalized = clampedPercent / 100.0;
            return lower + ((upper - lower) * normalized);
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
        connect(m_gauge, &QDial::valueChanged, this, [this](int raw)
        {
            if (m_settingGaugeProgrammatically || m_editable)
            {
                return;
            }

            const double mapped = PercentToConfiguredRange(raw, m_gaugeLowerLimit, m_gaugeUpperLimit);
            emit ControlDoubleEdited(m_key, mapped);
        });

        m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
        m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_progressBar, 0, 1, 1, 1);
        m_layout->addWidget(m_gauge, 0, 1, 1, 1);

        m_controlWidget = new TileControlWidget(m_type, this);
        m_controlWidget->setVisible(false);

        m_layout->addWidget(m_controlWidget, 1, 0, 1, 2);

        connect(m_controlWidget, &TileControlWidget::BoolEdited, this, [this](bool value)
        {
            emit ControlBoolEdited(m_key, value);
        });
        connect(m_controlWidget, &TileControlWidget::DoubleEdited, this, [this](double value)
        {
            emit ControlDoubleEdited(m_key, value);
        });
        connect(m_controlWidget, &TileControlWidget::StringEdited, this, [this](const QString& value)
        {
            emit ControlStringEdited(m_key, value);
        });

        // Control widgets should stay interactive in editable mode as well,
        // so we do not emit change on each drag operation from the tile itself.
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);

        setMinimumSize(40, 30);
        m_defaultSize = QSize(220, 84);
        SetGaugeProperties(m_gaugeLowerLimit, m_gaugeUpperLimit, m_gaugeTickInterval, m_gaugeShowTickMarks);
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetEditable(bool editable)
    {
        m_editable = editable;
        m_showEditHandles = m_editable && (m_editInteractionMode != EditInteractionMode::MoveOnly);

        // Edit mode is layout-only: never allow widget controls to send commands while moving/resizing.
        m_controlWidget->setEnabled(!m_editable);
        m_controlWidget->setAttribute(Qt::WA_TransparentForMouseEvents, m_editable);
        // Keep gauge visually consistent in editable mode; block interaction via mouse transparency.
        m_gauge->setEnabled(true);
        m_gauge->setAttribute(Qt::WA_TransparentForMouseEvents, m_editable);

        if (!m_editable)
        {
            m_dragMode = DragMode::None;
            m_isHovering = false;
            const bool gaugeInteractive = (m_type == VariableType::Double && m_widgetType == "double.gauge");
            setCursor(gaugeInteractive ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
        else
        {
            setCursor(Qt::OpenHandCursor);
        }

        update();
    }

    void VariableTile::SetShowEditHandles(bool showHandles)
    {
        m_showEditHandles = showHandles;
        update();
    }

    void VariableTile::SetSnapToGrid(bool enabled, int gridSize)
    {
        m_snapToGrid = enabled;
        if (gridSize >= 2)
        {
            m_gridSize = gridSize;
        }
    }

    void VariableTile::SetEditInteractionMode(EditInteractionMode mode)
    {
        m_editInteractionMode = mode;
        m_showEditHandles = m_editable && (m_editInteractionMode != EditInteractionMode::MoveOnly);
        update();
    }

    void VariableTile::SetDefaultSize(const QSize& size)
    {
        if (size.width() > 0 && size.height() > 0)
        {
            m_defaultSize = size;
        }
    }

    void VariableTile::SetGaugeProperties(double lowerLimit, double upperLimit, double tickInterval, bool showTickMarks)
    {
        double lower = lowerLimit;
        double upper = upperLimit;
        if (upper <= lower)
        {
            upper = lower + 0.001;
        }

        m_gaugeLowerLimit = lower;
        m_gaugeUpperLimit = upper;
        m_gaugeTickInterval = tickInterval;
        if (m_gaugeTickInterval <= 0.0)
        {
            m_gaugeTickInterval = 0.001;
        }
        m_gaugeShowTickMarks = showTickMarks;

        setProperty("gaugeLowerLimit", m_gaugeLowerLimit);
        setProperty("gaugeUpperLimit", m_gaugeUpperLimit);
        setProperty("gaugeTickInterval", m_gaugeTickInterval);
        setProperty("gaugeShowTickMarks", m_gaugeShowTickMarks);

        ApplyGaugeSettings();
        UpdateValueDisplay();
    }

    void VariableTile::paintEvent(QPaintEvent* event)
    {
        QFrame::paintEvent(event);

        if (!m_editable || !m_showEditHandles || !m_isHovering)
        {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const bool isDraggingOrResizing = (m_dragMode != DragMode::None);
        QPen borderPen(QColor(isDraggingOrResizing ? "#8a8a8a" : "#5a5a5a"));
        borderPen.setWidth(isDraggingOrResizing ? 2 : 1);
        borderPen.setStyle(Qt::SolidLine);
        painter.setPen(borderPen);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
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
            setFocus(Qt::MouseFocusReason);
            m_dragMode = HitTestDragMode(event->position().toPoint());
            m_dragStartGlobal = event->globalPosition().toPoint();
            m_dragStartGeometry = geometry();

            if (m_dragMode == DragMode::Move)
            {
                m_dragOrigin = event->globalPosition().toPoint() - frameGeometry().topLeft();
                setCursor(Qt::ClosedHandCursor);
            }
            else if (m_dragMode != DragMode::None)
            {
                UpdateCursorForPosition(event->position().toPoint());
            }

            update();
        }

        QFrame::mousePressEvent(event);
    }

    void VariableTile::mouseMoveEvent(QMouseEvent* event)
    {
        if (m_editable && (event->buttons() & Qt::LeftButton) && m_dragMode != DragMode::None)
        {
            if (m_dragMode == DragMode::Move)
            {
                QPoint nextPos = event->globalPosition().toPoint() - m_dragOrigin;
                if (m_snapToGrid && m_gridSize > 1)
                {
                    nextPos.setX((nextPos.x() / m_gridSize) * m_gridSize);
                    nextPos.setY((nextPos.y() / m_gridSize) * m_gridSize);
                }
                move(nextPos);
            }
            else
            {
                QRect next = m_dragStartGeometry;
                const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobal;
                const int minWidth = minimumWidth();
                const int minHeight = minimumHeight();

                auto resizeLeft = [&next, &delta, minWidth]()
                {
                    const int fixedRight = next.right();
                    int proposedLeft = next.left() + delta.x();
                    if ((fixedRight - proposedLeft + 1) < minWidth)
                    {
                        proposedLeft = fixedRight - minWidth + 1;
                    }
                    next.setLeft(proposedLeft);
                };
                auto resizeRight = [&next, &delta, minWidth]()
                {
                    const int fixedLeft = next.left();
                    int proposedRight = next.right() + delta.x();
                    if ((proposedRight - fixedLeft + 1) < minWidth)
                    {
                        proposedRight = fixedLeft + minWidth - 1;
                    }
                    next.setRight(proposedRight);
                };
                auto resizeTop = [&next, &delta, minHeight]()
                {
                    const int fixedBottom = next.bottom();
                    int proposedTop = next.top() + delta.y();
                    if ((fixedBottom - proposedTop + 1) < minHeight)
                    {
                        proposedTop = fixedBottom - minHeight + 1;
                    }
                    next.setTop(proposedTop);
                };
                auto resizeBottom = [&next, &delta, minHeight]()
                {
                    const int fixedTop = next.top();
                    int proposedBottom = next.bottom() + delta.y();
                    if ((proposedBottom - fixedTop + 1) < minHeight)
                    {
                        proposedBottom = fixedTop + minHeight - 1;
                    }
                    next.setBottom(proposedBottom);
                };

                switch (m_dragMode)
                {
                    case DragMode::ResizeLeft:
                        resizeLeft();
                        break;
                    case DragMode::ResizeRight:
                        resizeRight();
                        break;
                    case DragMode::ResizeTop:
                        resizeTop();
                        break;
                    case DragMode::ResizeBottom:
                        resizeBottom();
                        break;
                    case DragMode::ResizeTopLeft:
                        resizeLeft();
                        resizeTop();
                        break;
                    case DragMode::ResizeTopRight:
                        resizeRight();
                        resizeTop();
                        break;
                    case DragMode::ResizeBottomLeft:
                        resizeLeft();
                        resizeBottom();
                        break;
                    case DragMode::ResizeBottomRight:
                        resizeRight();
                        resizeBottom();
                        break;
                    case DragMode::Move:
                    case DragMode::None:
                        break;
                }

                if (m_snapToGrid && m_gridSize > 1)
                {
                    next.setX((next.x() / m_gridSize) * m_gridSize);
                    next.setY((next.y() / m_gridSize) * m_gridSize);
                    next.setWidth(((next.width() + m_gridSize / 2) / m_gridSize) * m_gridSize);
                    next.setHeight(((next.height() + m_gridSize / 2) / m_gridSize) * m_gridSize);
                }

                if (next.width() < minimumWidth())
                {
                    next.setWidth(minimumWidth());
                }
                if (next.height() < minimumHeight())
                {
                    next.setHeight(minimumHeight());
                }

                setGeometry(next);
            }

            update();
        }
        else if (m_editable)
        {
            UpdateCursorForPosition(event->position().toPoint());
        }

        QFrame::mouseMoveEvent(event);
    }

    void VariableTile::mouseReleaseEvent(QMouseEvent* event)
    {
        if (m_editable && event->button() == Qt::LeftButton)
        {
            m_dragMode = DragMode::None;
            UpdateCursorForPosition(event->position().toPoint());
            update();
        }

        QFrame::mouseReleaseEvent(event);
    }

    void VariableTile::enterEvent(QEnterEvent* event)
    {
        m_isHovering = true;
        if (m_editable)
        {
            UpdateCursorForPosition(event->position().toPoint());
            update();
        }

        QFrame::enterEvent(event);
    }

    void VariableTile::leaveEvent(QEvent* event)
    {
        m_isHovering = false;
        if (m_editable)
        {
            m_dragMode = DragMode::None;
            setCursor(Qt::ArrowCursor);
            update();
        }

        QFrame::leaveEvent(event);
    }

    void VariableTile::keyPressEvent(QKeyEvent* event)
    {
        if (!m_editable)
        {
            QFrame::keyPressEvent(event);
            return;
        }

        const int step = (event->modifiers() & Qt::ShiftModifier) ? 8 : 1;
        QRect next = geometry();
        bool handled = true;

        const bool resize = (event->modifiers() & Qt::ControlModifier);
        if (resize)
        {
            switch (event->key())
            {
                case Qt::Key_Left:
                    next.setWidth(next.width() - step);
                    break;
                case Qt::Key_Right:
                    next.setWidth(next.width() + step);
                    break;
                case Qt::Key_Up:
                    next.setHeight(next.height() - step);
                    break;
                case Qt::Key_Down:
                    next.setHeight(next.height() + step);
                    break;
                default:
                    handled = false;
                    break;
            }
        }
        else
        {
            switch (event->key())
            {
                case Qt::Key_Left:
                    next.translate(-step, 0);
                    break;
                case Qt::Key_Right:
                    next.translate(step, 0);
                    break;
                case Qt::Key_Up:
                    next.translate(0, -step);
                    break;
                case Qt::Key_Down:
                    next.translate(0, step);
                    break;
                default:
                    handled = false;
                    break;
            }
        }

        if (!handled)
        {
            QFrame::keyPressEvent(event);
            return;
        }

        if (next.width() < minimumWidth())
        {
            next.setWidth(minimumWidth());
        }
        if (next.height() < minimumHeight())
        {
            next.setHeight(minimumHeight());
        }

        if (m_snapToGrid && m_gridSize > 1)
        {
            next.setX((next.x() / m_gridSize) * m_gridSize);
            next.setY((next.y() / m_gridSize) * m_gridSize);
            next.setWidth(((next.width() + m_gridSize / 2) / m_gridSize) * m_gridSize);
            next.setHeight(((next.height() + m_gridSize / 2) / m_gridSize) * m_gridSize);
        }

        setGeometry(next);
        event->accept();
    }

    void VariableTile::contextMenuEvent(QContextMenuEvent* event)
    {
        if (!m_editable)
        {
            event->ignore();
            return;
        }

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
            action->setCheckable(true);
            action->setChecked(m_widgetType == widgetType);
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
                addWidgetAction("Checkbox control", "bool.checkbox");
                break;
            case VariableType::Double:
                addWidgetAction("Numeric text", "double.numeric");
                addWidgetAction("Progress bar", "double.progress");
                addWidgetAction("Gauge", "double.gauge");
                addWidgetAction("Slider control", "double.slider");
                break;
            case VariableType::String:
                addWidgetAction("Read-only label", "string.text");
                addWidgetAction("Read-only multiline", "string.multiline");
                addWidgetAction("Writable edit box", "string.edit");
                break;
            default:
                break;
        }

        QAction* propertiesAction = menu.addAction("Properties...");
        propertiesAction->setEnabled(IsPropertiesSupported());
        connect(propertiesAction, &QAction::triggered, this, [this]()
        {
            OpenPropertiesDialog();
        });

        QAction* sendToBackAction = menu.addAction("Send To Back");
        connect(sendToBackAction, &QAction::triggered, this, [this]()
        {
            lower();
        });

        QAction* resetSizeAction = menu.addAction("Reset Size");
        const bool isDefaultSize = (size() == m_defaultSize);
        resetSizeAction->setEnabled(!isDefaultSize);
        connect(resetSizeAction, &QAction::triggered, this, [this]()
        {
            resize(m_defaultSize);
        });

        QAction* removeAction = menu.addAction("Remove");
        connect(removeAction, &QAction::triggered, this, [this]()
        {
            emit RemoveRequested(m_key);
        });
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
        // Widget strategy selection:
        // map persisted widgetType id to one concrete visual presentation.
        const bool isBoolLed = (m_widgetType == "bool.led");
        const bool isBoolText = (m_widgetType == "bool.text");
        const bool isBoolCheckbox = (m_widgetType == "bool.checkbox");
        const bool isDoubleNumeric = (m_widgetType == "double.numeric");
        const bool isDoubleProgress = (m_widgetType == "double.progress");
        const bool isDoubleGauge = (m_widgetType == "double.gauge");
        const bool isDoubleSlider = (m_widgetType == "double.slider");
        const bool isStringText = (m_widgetType == "string.text");
        const bool isStringMultiline = (m_widgetType == "string.multiline");
        const bool isStringEdit = (m_widgetType == "string.edit");

        const bool showBoolLed = (m_type == VariableType::Bool && isBoolLed);
        const bool showBoolText = (m_type == VariableType::Bool && isBoolText);
        const bool showBoolCheckbox = (m_type == VariableType::Bool && isBoolCheckbox);
        const bool showDoubleNumeric = (m_type == VariableType::Double && isDoubleNumeric);
        const bool showDoubleProgress = (m_type == VariableType::Double && isDoubleProgress);
        const bool showDoubleGauge = (m_type == VariableType::Double && isDoubleGauge);
        const bool showDoubleSlider = (m_type == VariableType::Double && isDoubleSlider);
        const bool showStringText = (m_type == VariableType::String && isStringText);
        const bool showStringMultiline = (m_type == VariableType::String && isStringMultiline);
        const bool showStringEdit = (m_type == VariableType::String && isStringEdit);

        m_boolLed->setVisible(showBoolLed);
        m_progressBar->setVisible(showDoubleProgress);
        m_gauge->setVisible(showDoubleGauge);
        m_gauge->setCursor((showDoubleGauge && !m_editable) ? Qt::SizeHorCursor : Qt::ArrowCursor);

        if (!m_editable)
        {
            setCursor(showDoubleGauge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }

        const bool showValueLabel = showBoolText || showDoubleNumeric || showStringText || showStringMultiline;
        m_valueLabel->setVisible(showValueLabel);
        m_titleLabel->setVisible(!showDoubleGauge);

        const bool showControl = showBoolCheckbox || showDoubleSlider || showStringEdit;
        m_controlWidget->setVisible(showControl);

        if (showBoolCheckbox)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_controlWidget, 0, 1, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
        }
        else if (showControl)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_controlWidget, 1, 0, 1, 2);
        }
        else
        {
            m_layout->addWidget(m_controlWidget, 1, 0, 1, 2);
        }

        if (!showControl && showDoubleGauge)
        {
            m_layout->addWidget(m_gauge, 1, 0, 1, 2, Qt::AlignHCenter | Qt::AlignVCenter);
        }
        else if (!showControl && showDoubleProgress)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_progressBar, 1, 0, 1, 2);
        }
        else if (!showControl && showStringMultiline)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_valueLabel, 1, 0, 1, 2);
        }
        else if (!showControl)
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
        if (m_controlWidget->isVisible())
        {
            if (m_type == VariableType::Bool)
            {
                m_controlWidget->SetBoolValue(m_boolValue);
            }
            else if (m_type == VariableType::Double)
            {
                m_controlWidget->SetDoubleValue(m_doubleValue);
            }
            else if (m_type == VariableType::String)
            {
                m_controlWidget->SetStringValue(m_stringValue);
            }
            return;
        }

        if (m_progressBar->isVisible())
        {
            m_progressBar->setValue(DoubleToPercent(m_doubleValue));
            return;
        }

        if (m_gauge->isVisible())
        {
            m_settingGaugeProgrammatically = true;
            m_gauge->setValue(DoubleToPercent(m_doubleValue));
            m_settingGaugeProgrammatically = false;
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
        // Normalization algorithm: map configured [lower..upper] domain to 0..100 UI control range.
        double clamped = value;
        if (clamped < m_gaugeLowerLimit)
        {
            clamped = m_gaugeLowerLimit;
        }
        if (clamped > m_gaugeUpperLimit)
        {
            clamped = m_gaugeUpperLimit;
        }

        const double span = m_gaugeUpperLimit - m_gaugeLowerLimit;
        if (span <= 0.0)
        {
            return 0;
        }

        const double normalized = (clamped - m_gaugeLowerLimit) / span;
        return static_cast<int>(normalized * 100.0 + 0.5);
    }

    void VariableTile::UpdateBoolLedAppearance()
    {
        const QString color = m_boolValue ? "#2ecc71" : "#7f8c8d";
        m_boolLed->setStyleSheet(
            QString("background-color: %1; border-radius: 7px; border: 1px solid #4b4b4b;").arg(color)
        );
    }

    bool VariableTile::IsGaugeWidget() const
    {
        return (m_type == VariableType::Double && m_widgetType == "double.gauge");
    }

    bool VariableTile::IsPropertiesSupported() const
    {
        return IsGaugeWidget();
    }

    void VariableTile::OpenPropertiesDialog()
    {
        if (!IsGaugeWidget())
        {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle("Gauge Properties");

        auto* form = new QFormLayout(&dialog);

        auto* upperLimitSpin = new QDoubleSpinBox(&dialog);
        upperLimitSpin->setDecimals(3);
        upperLimitSpin->setRange(-1e6, 1e6);
        upperLimitSpin->setValue(m_gaugeUpperLimit);

        auto* lowerLimitSpin = new QDoubleSpinBox(&dialog);
        lowerLimitSpin->setDecimals(3);
        lowerLimitSpin->setRange(-1e6, 1e6);
        lowerLimitSpin->setValue(m_gaugeLowerLimit);

        auto* tickIntervalSpin = new QDoubleSpinBox(&dialog);
        tickIntervalSpin->setDecimals(3);
        tickIntervalSpin->setRange(0.001, 1e6);
        tickIntervalSpin->setValue(m_gaugeTickInterval);

        auto* showTickMarksCheck = new QCheckBox(&dialog);
        showTickMarksCheck->setChecked(m_gaugeShowTickMarks);

        form->addRow("Upper Limit", upperLimitSpin);
        form->addRow("Lower Limit", lowerLimitSpin);
        form->addRow("Tick Interval", tickIntervalSpin);
        form->addRow("Show Tick Marks", showTickMarksCheck);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }

        SetGaugeProperties(
            lowerLimitSpin->value(),
            upperLimitSpin->value(),
            tickIntervalSpin->value(),
            showTickMarksCheck->isChecked()
        );
    }

    void VariableTile::ApplyGaugeSettings()
    {
        if (m_gauge == nullptr)
        {
            return;
        }

        m_gauge->setNotchesVisible(m_gaugeShowTickMarks);

        const double span = m_gaugeUpperLimit - m_gaugeLowerLimit;
        if (span <= 0.0)
        {
            m_gauge->setSingleStep(1);
            m_gauge->setPageStep(10);
            return;
        }

        int step = static_cast<int>((m_gaugeTickInterval / span) * 100.0 + 0.5);
        if (step < 1)
        {
            step = 1;
        }
        if (step > 100)
        {
            step = 100;
        }

        m_gauge->setSingleStep(step);
        m_gauge->setPageStep(step);
    }

    VariableTile::DragMode VariableTile::HitTestDragMode(const QPoint& localPos) const
    {
        if (!m_editable)
        {
            return DragMode::None;
        }

        if (m_editInteractionMode == EditInteractionMode::MoveOnly)
        {
            return DragMode::Move;
        }

        const int margin = 8;
        const int x = localPos.x();
        const int y = localPos.y();
        const int maxX = width() - 1;
        const int maxY = height() - 1;

        const bool left = (x <= margin);
        const bool right = (x >= (maxX - margin));
        const bool top = (y <= margin);
        const bool bottom = (y >= (maxY - margin));

        if (m_editInteractionMode != EditInteractionMode::ResizeOnly && !(left || right || top || bottom))
        {
            return DragMode::Move;
        }

        if (left && top)
        {
            return DragMode::ResizeTopLeft;
        }
        if (right && top)
        {
            return DragMode::ResizeTopRight;
        }
        if (left && bottom)
        {
            return DragMode::ResizeBottomLeft;
        }
        if (right && bottom)
        {
            return DragMode::ResizeBottomRight;
        }
        if (left)
        {
            return DragMode::ResizeLeft;
        }
        if (right)
        {
            return DragMode::ResizeRight;
        }
        if (top)
        {
            return DragMode::ResizeTop;
        }
        if (bottom)
        {
            return DragMode::ResizeBottom;
        }

        if (m_editInteractionMode == EditInteractionMode::ResizeOnly)
        {
            return DragMode::None;
        }

        return DragMode::Move;
    }

    void VariableTile::UpdateCursorForPosition(const QPoint& localPos)
    {
        if (!m_editable)
        {
            setCursor(Qt::ArrowCursor);
            return;
        }

        switch (HitTestDragMode(localPos))
        {
            case DragMode::ResizeLeft:
            case DragMode::ResizeRight:
                setCursor(Qt::SizeHorCursor);
                break;
            case DragMode::ResizeTop:
            case DragMode::ResizeBottom:
                setCursor(Qt::SizeVerCursor);
                break;
            case DragMode::ResizeTopLeft:
            case DragMode::ResizeBottomRight:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case DragMode::ResizeTopRight:
            case DragMode::ResizeBottomLeft:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case DragMode::Move:
                setCursor(Qt::OpenHandCursor);
                break;
            case DragMode::None:
            default:
                setCursor(Qt::ArrowCursor);
                break;
        }
    }

}
