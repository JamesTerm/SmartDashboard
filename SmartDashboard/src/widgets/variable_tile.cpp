#include "widgets/variable_tile.h"

#include "widgets/line_plot_widget.h"
#include "widgets/tile_control_widget.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QDial>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QProgressBar>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>

namespace sd::widgets
{
    namespace
    {
        void DebugTileLog(const QString& line)
        {
            static_cast<void>(line);
        }

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
        m_boolLed->setStyleSheet("background-color: #7f8c8d; border-radius: 7px; border: 1px solid #4b4b4b;");

        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setMinimumHeight(8);
        m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

        m_linePlot = new LinePlotWidget(this);
        m_linePlot->setVisible(false);

        m_doubleEdit = new QLineEdit(this);
        m_doubleEdit->setVisible(false);
        connect(m_doubleEdit, &QLineEdit::editingFinished, this, [this]()
        {
            if (m_settingDoubleEditProgrammatically || m_editable)
            {
                return;
            }

            bool ok = false;
            const double parsed = m_doubleEdit->text().toDouble(&ok);
            if (ok)
            {
                SetDoubleValue(parsed);
                emit ControlDoubleEdited(m_key, parsed);
            }
            else
            {
                m_settingDoubleEditProgrammatically = true;
                m_doubleEdit->setText(QString::number(m_doubleValue, 'f', 4));
                m_settingDoubleEditProgrammatically = false;
            }
        });

        m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
        m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_layout->addWidget(m_progressBar, 0, 1, 1, 1);
        m_layout->addWidget(m_gauge, 0, 1, 1, 1);
        m_layout->addWidget(m_linePlot, 0, 1, 1, 1);
        m_layout->addWidget(m_doubleEdit, 0, 1, 1, 1);

        m_controlWidget = new TileControlWidget(m_type, this);
        m_controlWidget->setVisible(false);
        m_controlWidget->SetDoubleRange(m_sliderLowerLimit, m_sliderUpperLimit);
        m_controlWidget->SetDoubleTickSettings(m_sliderTickInterval, m_sliderShowTickMarks);

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
        SetProgressBarProperties(m_progressBarLowerLimit, m_progressBarUpperLimit);
        SetProgressBarShowPercentage(m_progressBarShowPercentage);
        SetProgressBarColors(m_progressBarForegroundColor, m_progressBarBackgroundColor);
        SetSliderProperties(m_sliderLowerLimit, m_sliderUpperLimit, m_sliderTickInterval, m_sliderShowTickMarks);
        SetLinePlotProperties(
            m_linePlotBufferSizeSamples,
            m_linePlotAutoYAxis,
            m_linePlotYLowerLimit,
            m_linePlotYUpperLimit
        );
        SetLinePlotNumberLinesVisible(m_linePlotShowNumberLines);
        SetLinePlotGridLinesVisible(m_linePlotShowGridLines);
        SetTextFontPointSize(m_textFontPointSize);
        SetDoubleNumericEditable(m_doubleNumericEditable);
        SetBoolCheckboxShowLabel(m_boolCheckboxShowLabel);
        SetBoolLedShowLabel(m_boolLedShowLabel);
        SetStringTextShowLabel(m_stringTextShowLabel);
        UpdateWidgetPresentation();
        UpdateValueDisplay();
        DebugTileLog(QString("tile.create key=%1 widget=%2 type=%3").arg(m_key).arg(m_widgetType).arg(static_cast<int>(m_type)));
    }

    void VariableTile::SetEditable(bool editable)
    {
        m_editable = editable;
        m_showEditHandles = m_editable && (m_editInteractionMode != EditInteractionMode::MoveOnly);

        // Edit mode is layout-only: never allow widget controls to send commands while moving/resizing.
        m_controlWidget->SetInteractionEnabled(!m_editable);
        m_controlWidget->setAttribute(Qt::WA_TransparentForMouseEvents, m_editable);
        m_doubleEdit->setEnabled(!m_editable && m_hasValue);
        m_doubleEdit->setAttribute(Qt::WA_TransparentForMouseEvents, m_editable);
        // Keep gauge visually consistent in editable mode; block interaction via mouse transparency.
        m_gauge->setEnabled(m_hasValue);
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

    void VariableTile::ClearValue()
    {
        m_hasValue = false;
        m_valueOrigin = ValueOrigin::None;
        UpdateWidgetPresentation();
        UpdateValueDisplay();
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

    void VariableTile::SetProgressBarProperties(double lowerLimit, double upperLimit)
    {
        double lower = lowerLimit;
        double upper = upperLimit;
        if (upper <= lower)
        {
            upper = lower + 0.001;
        }

        m_progressBarLowerLimit = lower;
        m_progressBarUpperLimit = upper;
        setProperty("progressBarLowerLimit", m_progressBarLowerLimit);
        setProperty("progressBarUpperLimit", m_progressBarUpperLimit);

        ApplyProgressBarSettings();
        UpdateValueDisplay();
    }

    void VariableTile::SetProgressBarShowPercentage(bool showPercentage)
    {
        m_progressBarShowPercentage = showPercentage;
        setProperty("progressBarShowPercentage", m_progressBarShowPercentage);
        ApplyProgressBarSettings();
        UpdateWidgetPresentation();
    }

    void VariableTile::SetProgressBarColors(const QString& foregroundColor, const QString& backgroundColor)
    {
        const QColor foreground(foregroundColor);
        if (foreground.isValid())
        {
            m_progressBarForegroundColor = foreground.name(QColor::HexRgb);
            setProperty("progressBarForegroundColor", m_progressBarForegroundColor);
        }

        const QColor background(backgroundColor);
        if (background.isValid())
        {
            m_progressBarBackgroundColor = background.name(QColor::HexRgb);
            setProperty("progressBarBackgroundColor", m_progressBarBackgroundColor);
        }

        ApplyProgressBarSettings();
    }

    void VariableTile::SetSliderProperties(double lowerLimit, double upperLimit, double tickInterval, bool showTickMarks)
    {
        double lower = lowerLimit;
        double upper = upperLimit;
        if (upper <= lower)
        {
            upper = lower + 0.001;
        }

        m_sliderLowerLimit = lower;
        m_sliderUpperLimit = upper;
        m_sliderTickInterval = tickInterval;
        if (m_sliderTickInterval <= 0.0)
        {
            m_sliderTickInterval = 0.001;
        }
        m_sliderShowTickMarks = showTickMarks;

        setProperty("sliderLowerLimit", m_sliderLowerLimit);
        setProperty("sliderUpperLimit", m_sliderUpperLimit);
        setProperty("sliderTickInterval", m_sliderTickInterval);
        setProperty("sliderShowTickMarks", m_sliderShowTickMarks);

        ApplySliderSettings();
        UpdateValueDisplay();
    }

    void VariableTile::SetLinePlotProperties(int bufferSizeSamples, bool autoYAxis, double yLowerLimit, double yUpperLimit)
    {
        int bufferSize = bufferSizeSamples;
        if (bufferSize < 2)
        {
            bufferSize = 2;
        }

        double lower = yLowerLimit;
        double upper = yUpperLimit;
        if (upper <= lower)
        {
            upper = lower + 0.001;
        }

        m_linePlotBufferSizeSamples = bufferSize;
        m_linePlotAutoYAxis = autoYAxis;
        m_linePlotYLowerLimit = lower;
        m_linePlotYUpperLimit = upper;

        setProperty("linePlotBufferSizeSamples", m_linePlotBufferSizeSamples);
        setProperty("linePlotAutoYAxis", m_linePlotAutoYAxis);
        setProperty("linePlotYLowerLimit", m_linePlotYLowerLimit);
        setProperty("linePlotYUpperLimit", m_linePlotYUpperLimit);

        ApplyLinePlotSettings();
    }

    void VariableTile::SetLinePlotNumberLinesVisible(bool visible)
    {
        m_linePlotShowNumberLines = visible;
        setProperty("linePlotShowNumberLines", m_linePlotShowNumberLines);
        ApplyLinePlotSettings();
    }

    void VariableTile::SetLinePlotGridLinesVisible(bool visible)
    {
        m_linePlotShowGridLines = visible;
        setProperty("linePlotShowGridLines", m_linePlotShowGridLines);
        ApplyLinePlotSettings();
    }

    void VariableTile::SetDoubleNumericEditable(bool editable)
    {
        m_doubleNumericEditable = editable;
        setProperty("doubleNumericEditable", m_doubleNumericEditable);

        if (m_doubleEdit != nullptr)
        {
            m_settingDoubleEditProgrammatically = true;
            m_doubleEdit->setText(QString::number(m_doubleValue, 'f', 4));
            m_settingDoubleEditProgrammatically = false;
        }

        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetTextFontPointSize(int pointSize)
    {
        m_textFontPointSize = (pointSize > 0) ? pointSize : 0;

        if (m_textFontPointSize > 0)
        {
            setProperty("textFontPointSize", m_textFontPointSize);
        }
        else
        {
            setProperty("textFontPointSize", QVariant());
        }

        QFont valueFont = font();
        if (m_textFontPointSize > 0)
        {
            valueFont.setPointSize(m_textFontPointSize);
        }

        if (m_valueLabel != nullptr)
        {
            m_valueLabel->setFont(valueFont);
        }
        if (m_doubleEdit != nullptr)
        {
            m_doubleEdit->setFont(valueFont);
        }

        if (m_controlWidget != nullptr)
        {
            m_controlWidget->SetTextFontPointSize(m_textFontPointSize);
        }

        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetBoolCheckboxShowLabel(bool showLabel)
    {
        m_boolCheckboxShowLabel = showLabel;
        setProperty("boolCheckboxShowLabel", m_boolCheckboxShowLabel);
        UpdateWidgetPresentation();
    }

    // Ian: Optional label for the LED indicator widget.  Follows the same
    // pattern as SetBoolCheckboxShowLabel — stores value, sets Qt dynamic
    // property (for layout serialization), and triggers a presentation update.
    // Default is true (label visible) to preserve existing behavior.
    void VariableTile::SetBoolLedShowLabel(bool showLabel)
    {
        m_boolLedShowLabel = showLabel;
        setProperty("boolLedShowLabel", m_boolLedShowLabel);
        UpdateWidgetPresentation();
    }

    // Ian: Optional label for the read-only string text widget.  When hidden,
    // the value occupies the full tile width with no column-0 gap.  When shown,
    // column-0 min width is reduced to let Qt size the label naturally instead
    // of forcing 90px, closing the visual gap between label and value.
    void VariableTile::SetStringTextShowLabel(bool showLabel)
    {
        m_stringTextShowLabel = showLabel;
        setProperty("stringTextShowLabel", m_stringTextShowLabel);
        UpdateWidgetPresentation();
    }

    void VariableTile::SetStringChooserMode(bool chooserMode)
    {
        if (m_stringChooserMode == chooserMode)
        {
            return;
        }

        m_stringChooserMode = chooserMode;
        DebugTileLog(QString("tile.set_chooser_mode key=%1 chooser=%2 widget=%3").arg(m_key).arg(m_stringChooserMode ? 1 : 0).arg(m_widgetType));
        setProperty("stringChooserMode", m_stringChooserMode);
        if (m_controlWidget != nullptr)
        {
            m_controlWidget->SetStringChooserMode(m_stringChooserMode);
        }
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetStringChooserOptions(const QStringList& options)
    {
        m_stringChooserOptions = options;
        DebugTileLog(QString("tile.set_chooser_options key=%1 count=%2 widget=%3").arg(m_key).arg(options.size()).arg(m_widgetType));
        setProperty("stringChooserOptions", m_stringChooserOptions);
        if (m_controlWidget != nullptr)
        {
            m_controlWidget->SetStringOptions(m_stringChooserOptions);
        }
        UpdateValueDisplay();
    }

    void VariableTile::ResetLinePlotGraph()
    {
        if (m_linePlot != nullptr)
        {
            m_linePlot->ResetGraph();
        }
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
        SetBoolValueInternal(value, ValueOrigin::LiveTransport);
    }

    void VariableTile::SetDoubleValue(double value)
    {
        SetDoubleValueInternal(value, ValueOrigin::LiveTransport);
    }

    void VariableTile::SetStringValue(const QString& value)
    {
        SetStringValueInternal(value, ValueOrigin::LiveTransport);
    }

    void VariableTile::SetTemporaryDefaultBoolValue(bool value)
    {
        if (m_hasValue && m_valueOrigin != ValueOrigin::TemporaryDefault)
        {
            return;
        }

        SetBoolValueInternal(value, ValueOrigin::TemporaryDefault);
    }

    void VariableTile::SetTemporaryDefaultDoubleValue(double value)
    {
        if (m_hasValue && m_valueOrigin != ValueOrigin::TemporaryDefault)
        {
            return;
        }

        SetDoubleValueInternal(value, ValueOrigin::TemporaryDefault);
    }

    void VariableTile::SetTemporaryDefaultStringValue(const QString& value)
    {
        if (m_hasValue && m_valueOrigin != ValueOrigin::TemporaryDefault)
        {
            return;
        }

        SetStringValueInternal(value, ValueOrigin::TemporaryDefault);
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

    bool VariableTile::HasValue() const
    {
        return m_hasValue;
    }

    bool VariableTile::HasLiveValue() const
    {
        return m_hasValue && m_valueOrigin != ValueOrigin::TemporaryDefault;
    }

    bool VariableTile::IsShowingTemporaryDefault() const
    {
        return m_hasValue && m_valueOrigin == ValueOrigin::TemporaryDefault;
    }

    ValueOrigin VariableTile::GetValueOrigin() const
    {
        return m_valueOrigin;
    }

    bool VariableTile::GetBoolValue() const
    {
        return m_boolValue;
    }

    double VariableTile::GetDoubleValue() const
    {
        return m_doubleValue;
    }

    QString VariableTile::GetStringValue() const
    {
        return m_stringValue;
    }

    bool VariableTile::GetStringChooserMode() const
    {
        return m_stringChooserMode;
    }

    QStringList VariableTile::GetStringChooserOptions() const
    {
        return m_stringChooserOptions;
    }

    void VariableTile::SetWidgetType(const QString& widgetType)
    {
        m_widgetType = widgetType;
        setProperty("widgetType", m_widgetType);

        if (m_widgetType == "double.lineplot")
        {
            const int recommendedHeight = 140;
            if (height() < recommendedHeight)
            {
                resize(width(), recommendedHeight);
            }
        }

        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetTitleText(const QString& title)
    {
        if (m_titleLabel == nullptr)
        {
            return;
        }

        m_titleLabel->setText(title);
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
            QMenu menu(this);
            if (IsLinePlotWidget())
            {
                QAction* resetPlotAction = menu.addAction("Reset Graph");
                connect(resetPlotAction, &QAction::triggered, this, [this]()
                {
                    ResetLinePlotGraph();
                });
            }

            if (menu.actions().isEmpty())
            {
                event->ignore();
                return;
            }

            menu.exec(event->globalPos());
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
                addWidgetAction("Line Plot", "double.lineplot");
                addWidgetAction("Slider control", "double.slider");
                break;
            case VariableType::String:
                addWidgetAction("Read-only label", "string.text");
                addWidgetAction("Read-only multiline", "string.multiline");
                addWidgetAction("Writable edit box", "string.edit");
                addWidgetAction("Chooser dropdown", "string.chooser");
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

    void VariableTile::SetBoolValueInternal(bool value, ValueOrigin origin)
    {
        m_hasValue = true;
        m_valueOrigin = origin;
        m_boolValue = value;
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetDoubleValueInternal(double value, ValueOrigin origin)
    {
        m_hasValue = true;
        m_valueOrigin = origin;
        m_doubleValue = value;
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::SetStringValueInternal(const QString& value, ValueOrigin origin)
    {
        m_hasValue = true;
        m_valueOrigin = origin;
        m_stringValue = value;
        UpdateWidgetPresentation();
        UpdateValueDisplay();
    }

    void VariableTile::UpdateWidgetPresentation()
    {
        m_layout->setVerticalSpacing(4);

        if (!m_hasValue)
        {
            m_boolLed->setVisible(false);
            m_progressBar->setVisible(false);
            m_gauge->setVisible(false);
            m_linePlot->setVisible(false);
            m_doubleEdit->setVisible(false);
            m_controlWidget->setVisible(false);
            m_controlWidget->SetValueAvailable(false);
            m_titleLabel->setVisible(true);
            m_valueLabel->setVisible(true);
            m_valueLabel->setWordWrap(false);
            m_valueLabel->setMinimumHeight(0);
            m_layout->setColumnMinimumWidth(0, 90);
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 0);
            m_doubleEdit->setEnabled(false);
            m_gauge->setEnabled(false);
            return;
        }

        if (m_valueOrigin == ValueOrigin::TemporaryDefault)
        {
            m_valueLabel->setStyleSheet("font-weight: 600; color: #8a94a6;");
        }
        else
        {
            m_valueLabel->setStyleSheet("font-weight: 600;");
        }

        // Widget strategy selection:
        // map persisted widgetType id to one concrete visual presentation.
        const bool isBoolLed = (m_widgetType == "bool.led");
        const bool isBoolText = (m_widgetType == "bool.text");
        const bool isBoolCheckbox = (m_widgetType == "bool.checkbox");
        const bool isDoubleNumeric = (m_widgetType == "double.numeric");
        const bool isDoubleProgress = (m_widgetType == "double.progress");
        const bool isDoubleGauge = (m_widgetType == "double.gauge");
        const bool isDoubleLinePlot = (m_widgetType == "double.lineplot");
        const bool isDoubleSlider = (m_widgetType == "double.slider");
        const bool isStringText = (m_widgetType == "string.text");
        const bool isStringMultiline = (m_widgetType == "string.multiline");
        const bool isStringEdit = (m_widgetType == "string.edit");
        const bool isStringChooser = (m_widgetType == "string.chooser");

        const bool showBoolLed = (m_type == VariableType::Bool && isBoolLed);
        const bool showBoolText = (m_type == VariableType::Bool && isBoolText);
        const bool showBoolCheckbox = (m_type == VariableType::Bool && isBoolCheckbox);
        const bool showDoubleNumeric = (m_type == VariableType::Double && isDoubleNumeric);
        const bool showDoubleProgress = (m_type == VariableType::Double && isDoubleProgress);
        const bool showDoubleGauge = (m_type == VariableType::Double && isDoubleGauge);
        const bool showDoubleLinePlot = (m_type == VariableType::Double && isDoubleLinePlot);
        const bool showDoubleSlider = (m_type == VariableType::Double && isDoubleSlider);
        const bool showStringText = (m_type == VariableType::String && isStringText);
        const bool showStringMultiline = (m_type == VariableType::String && isStringMultiline);
        const bool showStringEdit = (m_type == VariableType::String && isStringEdit);
        const bool showStringChooser = (m_type == VariableType::String && isStringChooser);

        m_stringChooserMode = showStringChooser;
        m_controlWidget->SetStringChooserMode(m_stringChooserMode);
        m_controlWidget->SetStringOptions(m_stringChooserOptions);
        m_controlWidget->SetValueAvailable(true);

        m_boolLed->setVisible(showBoolLed);
        if (showBoolLed)
        {
            UpdateBoolLedAppearance();
        }
        m_progressBar->setVisible(showDoubleProgress);
        m_gauge->setVisible(showDoubleGauge);
        m_linePlot->setVisible(showDoubleLinePlot);
        m_gauge->setCursor((showDoubleGauge && !m_editable) ? Qt::SizeHorCursor : Qt::ArrowCursor);

        if (!m_editable)
        {
            setCursor(showDoubleGauge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }

        const bool showValueLabel = showBoolText || (showDoubleNumeric && !m_doubleNumericEditable) || showStringText || showStringMultiline;
        m_valueLabel->setVisible(showValueLabel);
        const bool showDoubleEdit = (showDoubleNumeric && m_doubleNumericEditable);
        m_doubleEdit->setVisible(showDoubleEdit);
        if (showDoubleEdit)
        {
            m_settingDoubleEditProgrammatically = true;
            m_doubleEdit->setText(QString::number(m_doubleValue, 'f', 4));
            m_settingDoubleEditProgrammatically = false;
        }
        m_doubleEdit->setEnabled(showDoubleEdit && !m_editable && m_hasValue);
        m_titleLabel->setVisible(!showDoubleGauge);

        const bool showControl = showBoolCheckbox || showDoubleSlider || showStringEdit || showStringChooser;
        m_controlWidget->setVisible(showControl);

        if (showBoolCheckbox)
        {
            m_titleLabel->setVisible(m_boolCheckboxShowLabel);
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_controlWidget, 0, 1, 1, 1, Qt::AlignRight | Qt::AlignVCenter);

            if (!m_boolCheckboxShowLabel)
            {
                // Ian: Remember the current width before compacting so we can
                // restore the user's layout when the label is shown again.
                if (m_widthBeforeCompact == 0)
                {
                    m_widthBeforeCompact = width();
                }
                m_layout->setColumnMinimumWidth(0, 0);
                const QMargins margins = m_layout->contentsMargins();
                const int compactWidth = m_controlWidget->sizeHint().width() + margins.left() + margins.right() + 8;
                if (width() > compactWidth)
                {
                    resize(compactWidth, height());
                }
            }
            else
            {
                m_layout->setColumnMinimumWidth(0, 90);
                // Ian: Only force a width restore when recovering from a
                // hide→show cycle (m_widthBeforeCompact > 0).  When there
                // was no prior hide (e.g. first value arrives after layout
                // load with showLabel already true), the tile's current
                // geometry is the user's saved size — don't override it
                // with the 140px default.  This fixes the bug where a
                // tile intentionally sized to e.g. 104px would jump to
                // 140px on first transport value.
                if (m_widthBeforeCompact > 0)
                {
                    const int restoreWidth = m_widthBeforeCompact;
                    m_widthBeforeCompact = 0;
                    if (width() < restoreWidth)
                    {
                        resize(restoreWidth, height());
                    }
                }
            }
        }
        else if (showControl)
        {
            m_layout->setColumnMinimumWidth(0, 90);
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_controlWidget, 1, 0, 1, 2);
        }
        else
        {
            m_layout->setColumnMinimumWidth(0, 90);
            m_layout->addWidget(m_controlWidget, 1, 0, 1, 2);
        }

        if (!showControl && showDoubleGauge)
        {
            m_layout->addWidget(m_gauge, 1, 0, 1, 2, Qt::AlignHCenter | Qt::AlignVCenter);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 1);
        }
        else if (!showControl && showDoubleLinePlot)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignHCenter | Qt::AlignVCenter);
            m_layout->addWidget(m_linePlot, 1, 0, 1, 2);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 1);
        }
        else if (!showControl && showDoubleProgress)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_progressBar, 1, 0, 1, 2);
            m_layout->setVerticalSpacing(0);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 1);

            if (!m_progressBarShowPercentage)
            {
                const QMargins margins = m_layout->contentsMargins();
                const int compactWidth = 120 + margins.left() + margins.right();
                if (width() > compactWidth)
                {
                    resize(compactWidth, height());
                }
            }
        }
        else if (!showControl && showStringMultiline)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
            m_layout->addWidget(m_valueLabel, 1, 0, 1, 2);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 0);
        }
        else if (!showControl && showBoolLed)
        {
            // Ian: LED indicator optional label — mirrors the checkbox pattern.
            // When label is hidden the LED dot occupies the full tile width
            // (left-aligned) and the tile auto-shrinks to compact size.
            // m_widthBeforeCompact remembers the pre-hide width so toggling
            // the label back on restores the user's layout.
            m_titleLabel->setVisible(m_boolLedShowLabel);
            if (m_boolLedShowLabel)
            {
                m_layout->setColumnMinimumWidth(0, 90);
                m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
                m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
                // Ian: Same fix as checkbox — only restore width when
                // recovering from a hide→show cycle.  Don't force 140px
                // on tiles that were loaded with a user-chosen smaller width.
                if (m_widthBeforeCompact > 0)
                {
                    const int restoreWidth = m_widthBeforeCompact;
                    m_widthBeforeCompact = 0;
                    if (width() < restoreWidth)
                    {
                        resize(restoreWidth, height());
                    }
                }
            }
            else
            {
                if (m_widthBeforeCompact == 0)
                {
                    m_widthBeforeCompact = width();
                }
                m_layout->setColumnMinimumWidth(0, 0);
                m_layout->addWidget(m_boolLed, 0, 0, 1, 2, Qt::AlignCenter);
                const QMargins margins = m_layout->contentsMargins();
                const int compactWidth = m_boolLed->sizeHint().width() + margins.left() + margins.right() + 8;
                if (width() > compactWidth)
                {
                    resize(compactWidth, height());
                }
            }
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 0);
        }
        else if (!showControl && showStringText)
        {
            // Ian: Read-only label optional label with gap fix.  When the label
            // is shown, column-0 min width is set to 0 so the label column
            // sizes naturally to its text width — closing the 90px gap that
            // used to push the value far to the right.  When hidden, the value
            // spans both columns for full-width display.
            m_titleLabel->setVisible(m_stringTextShowLabel);
            if (m_stringTextShowLabel)
            {
                m_layout->setColumnMinimumWidth(0, 0);
                m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
                m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
            }
            else
            {
                m_layout->setColumnMinimumWidth(0, 0);
                m_layout->addWidget(m_valueLabel, 0, 0, 1, 2);
            }
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 0);
        }
        else if (!showControl)
        {
            m_layout->addWidget(m_titleLabel, 0, 0, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_valueLabel, 0, 1, 1, 1);
            m_layout->addWidget(m_doubleEdit, 0, 1, 1, 1);
            m_layout->addWidget(m_boolLed, 0, 1, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_layout->addWidget(m_progressBar, 0, 1, 1, 1);
            m_layout->addWidget(m_gauge, 0, 1, 1, 1);
            m_layout->addWidget(m_linePlot, 0, 1, 1, 1);
            m_layout->setRowStretch(0, 0);
            m_layout->setRowStretch(1, 0);
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
        if (!m_hasValue)
        {
            m_valueLabel->setStyleSheet("font-weight: 600;");
            m_valueLabel->setText("No data");
            return;
        }

        const bool isBoolCheckbox = (m_type == VariableType::Bool && m_widgetType == "bool.checkbox");
        const bool isDoubleSlider = (m_type == VariableType::Double && m_widgetType == "double.slider");
        const bool isStringEdit = (m_type == VariableType::String && m_widgetType == "string.edit");
        const bool isStringChooser = (m_type == VariableType::String && m_widgetType == "string.chooser");
        const bool usesControlWidget = isBoolCheckbox || isDoubleSlider || isStringEdit || isStringChooser;

        if (usesControlWidget)
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

        const bool usesDoubleEdit = (m_type == VariableType::Double && m_widgetType == "double.numeric" && m_doubleNumericEditable);
        if (usesDoubleEdit)
        {
            m_settingDoubleEditProgrammatically = true;
            m_doubleEdit->setText(QString::number(m_doubleValue, 'f', 4));
            m_settingDoubleEditProgrammatically = false;
            return;
        }

        if (m_type == VariableType::Double && m_widgetType == "double.progress")
        {
            m_progressBar->setValue(ValueToPercentForProgressBar(m_doubleValue));
            return;
        }

        if (m_type == VariableType::Double && m_widgetType == "double.gauge")
        {
            m_settingGaugeProgrammatically = true;
            m_gauge->setValue(DoubleToPercent(m_doubleValue));
            m_settingGaugeProgrammatically = false;
            return;
        }

        if (m_type == VariableType::Double && m_widgetType == "double.lineplot")
        {
            m_linePlot->AddSample(m_doubleValue);
            return;
        }

        if (m_type == VariableType::Bool && m_widgetType == "bool.led")
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

    int VariableTile::ValueToPercentForProgressBar(double value) const
    {
        double clamped = value;
        if (clamped < m_progressBarLowerLimit)
        {
            clamped = m_progressBarLowerLimit;
        }
        if (clamped > m_progressBarUpperLimit)
        {
            clamped = m_progressBarUpperLimit;
        }

        const double span = m_progressBarUpperLimit - m_progressBarLowerLimit;
        if (span <= 0.0)
        {
            return 0;
        }

        const double normalized = (clamped - m_progressBarLowerLimit) / span;
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

    bool VariableTile::IsLinePlotWidget() const
    {
        return (m_type == VariableType::Double && m_widgetType == "double.lineplot");
    }

    bool VariableTile::IsDoubleNumericWidget() const
    {
        return (m_type == VariableType::Double && m_widgetType == "double.numeric");
    }

    bool VariableTile::IsPropertiesSupported() const
    {
        const bool isProgressBar = (m_widgetType == "double.progress");
        const bool isSlider = (m_widgetType == "double.slider");
        const bool isBoolCheckbox = (m_widgetType == "bool.checkbox");
        const bool isBoolLed = (m_widgetType == "bool.led");
        const bool isTextDisplay =
            (m_widgetType == "bool.text") ||
            (m_widgetType == "string.text") ||
            (m_widgetType == "string.multiline") ||
            (m_widgetType == "string.edit");
        return IsGaugeWidget() || IsLinePlotWidget() || IsDoubleNumericWidget() || isProgressBar || isSlider || isBoolCheckbox || isBoolLed || isTextDisplay;
    }

    void VariableTile::OpenPropertiesDialog()
    {
        if (IsGaugeWidget())
        {
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
            return;
        }

        if (m_widgetType == "double.progress")
        {
            QDialog dialog(this);
            dialog.setWindowTitle("Progress Bar Properties");

            auto* form = new QFormLayout(&dialog);

            auto* upperLimitSpin = new QDoubleSpinBox(&dialog);
            upperLimitSpin->setDecimals(3);
            upperLimitSpin->setRange(-1e6, 1e6);
            upperLimitSpin->setValue(m_progressBarUpperLimit);

            auto* lowerLimitSpin = new QDoubleSpinBox(&dialog);
            lowerLimitSpin->setDecimals(3);
            lowerLimitSpin->setRange(-1e6, 1e6);
            lowerLimitSpin->setValue(m_progressBarLowerLimit);

            form->addRow("Upper Limit", upperLimitSpin);
            form->addRow("Lower Limit", lowerLimitSpin);

            auto* showPercentCheck = new QCheckBox(&dialog);
            showPercentCheck->setChecked(m_progressBarShowPercentage);
            form->addRow("Show Percentage", showPercentCheck);

            auto* foregroundButton = new QPushButton(m_progressBarForegroundColor, &dialog);
            foregroundButton->setStyleSheet(QString("background:%1;").arg(m_progressBarForegroundColor));
            auto* backgroundButton = new QPushButton(m_progressBarBackgroundColor, &dialog);
            backgroundButton->setStyleSheet(QString("background:%1;").arg(m_progressBarBackgroundColor));
            form->addRow("Foreground", foregroundButton);
            form->addRow("Background", backgroundButton);

            QString selectedForeground = m_progressBarForegroundColor;
            QString selectedBackground = m_progressBarBackgroundColor;

            connect(foregroundButton, &QPushButton::clicked, &dialog, [foregroundButton, &selectedForeground, this]()
            {
                const QColor picked = QColorDialog::getColor(QColor(selectedForeground), this, "Choose Progress Foreground");
                if (!picked.isValid())
                {
                    return;
                }

                selectedForeground = picked.name(QColor::HexRgb);
                foregroundButton->setText(selectedForeground);
                foregroundButton->setStyleSheet(QString("background:%1;").arg(selectedForeground));
            });

            connect(backgroundButton, &QPushButton::clicked, &dialog, [backgroundButton, &selectedBackground, this]()
            {
                const QColor picked = QColorDialog::getColor(QColor(selectedBackground), this, "Choose Progress Background");
                if (!picked.isValid())
                {
                    return;
                }

                selectedBackground = picked.name(QColor::HexRgb);
                backgroundButton->setText(selectedBackground);
                backgroundButton->setStyleSheet(QString("background:%1;").arg(selectedBackground));
            });

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetProgressBarProperties(
                lowerLimitSpin->value(),
                upperLimitSpin->value()
            );
            SetProgressBarShowPercentage(showPercentCheck->isChecked());
            SetProgressBarColors(selectedForeground, selectedBackground);
            return;
        }

        if (m_widgetType == "double.slider")
        {
            QDialog dialog(this);
            dialog.setWindowTitle("Slider Properties");

            auto* form = new QFormLayout(&dialog);

            auto* upperLimitSpin = new QDoubleSpinBox(&dialog);
            upperLimitSpin->setDecimals(3);
            upperLimitSpin->setRange(-1e6, 1e6);
            upperLimitSpin->setValue(m_sliderUpperLimit);

            auto* lowerLimitSpin = new QDoubleSpinBox(&dialog);
            lowerLimitSpin->setDecimals(3);
            lowerLimitSpin->setRange(-1e6, 1e6);
            lowerLimitSpin->setValue(m_sliderLowerLimit);

            auto* tickIntervalSpin = new QDoubleSpinBox(&dialog);
            tickIntervalSpin->setDecimals(3);
            tickIntervalSpin->setRange(0.001, 1e6);
            tickIntervalSpin->setValue(m_sliderTickInterval);

            auto* showTickMarksCheck = new QCheckBox(&dialog);
            showTickMarksCheck->setChecked(m_sliderShowTickMarks);

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

            SetSliderProperties(
                lowerLimitSpin->value(),
                upperLimitSpin->value(),
                tickIntervalSpin->value(),
                showTickMarksCheck->isChecked()
            );
            return;
        }

        if (IsDoubleNumericWidget())
        {
            QDialog dialog(this);
            dialog.setWindowTitle("Numeric Text Properties");

            auto* form = new QFormLayout(&dialog);
            auto* editableCheck = new QCheckBox(&dialog);
            editableCheck->setChecked(m_doubleNumericEditable);
            form->addRow("Editable", editableCheck);

            auto* fontSizeSpin = new QSpinBox(&dialog);
            fontSizeSpin->setRange(0, 96);
            fontSizeSpin->setSpecialValueText("Default");
            fontSizeSpin->setValue(m_textFontPointSize);
            form->addRow("Font Size", fontSizeSpin);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetDoubleNumericEditable(editableCheck->isChecked());
            SetTextFontPointSize(fontSizeSpin->value());
            return;
        }

        if (m_widgetType == "bool.text" || m_widgetType == "string.text" || m_widgetType == "string.multiline")
        {
            QDialog dialog(this);
            dialog.setWindowTitle("Text Properties");

            auto* form = new QFormLayout(&dialog);
            auto* fontSizeSpin = new QSpinBox(&dialog);
            fontSizeSpin->setRange(0, 96);
            fontSizeSpin->setSpecialValueText("Default");
            fontSizeSpin->setValue(m_textFontPointSize);
            form->addRow("Font Size", fontSizeSpin);

            // Ian: Show Label option only for string.text — the read-only label
            // widget.  bool.text and string.multiline always show their label.
            QCheckBox* showLabelCheck = nullptr;
            if (m_widgetType == "string.text")
            {
                showLabelCheck = new QCheckBox(&dialog);
                showLabelCheck->setChecked(m_stringTextShowLabel);
                form->addRow("Show Label", showLabelCheck);
            }

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetTextFontPointSize(fontSizeSpin->value());
            if (showLabelCheck != nullptr)
            {
                SetStringTextShowLabel(showLabelCheck->isChecked());
            }
            return;
        }

        if (m_widgetType == "string.edit" || m_widgetType == "string.chooser")
        {
            QDialog dialog(this);
            dialog.setWindowTitle(m_widgetType == "string.chooser" ? "Chooser Properties" : "Text Edit Properties");

            auto* form = new QFormLayout(&dialog);
            auto* fontSizeSpin = new QSpinBox(&dialog);
            fontSizeSpin->setRange(0, 96);
            fontSizeSpin->setSpecialValueText("Default");
            fontSizeSpin->setValue(m_textFontPointSize);
            form->addRow("Font Size", fontSizeSpin);

            QLineEdit* optionsEdit = nullptr;
            if (m_widgetType == "string.chooser")
            {
                optionsEdit = new QLineEdit(&dialog);
                optionsEdit->setPlaceholderText("Option1, Option2, Option3");
                optionsEdit->setText(m_stringChooserOptions.join(","));
                form->addRow("Options", optionsEdit);
            }

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetTextFontPointSize(fontSizeSpin->value());
            if (m_widgetType == "string.chooser" && optionsEdit != nullptr)
            {
                const QStringList rawOptions = optionsEdit->text().split(',', Qt::SkipEmptyParts);
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
                SetStringChooserOptions(options);
            }
            return;
        }

        if (m_widgetType == "bool.led")
        {
            QDialog dialog(this);
            dialog.setWindowTitle("LED Indicator Properties");

            auto* form = new QFormLayout(&dialog);
            auto* showLabelCheck = new QCheckBox(&dialog);
            showLabelCheck->setChecked(m_boolLedShowLabel);
            form->addRow("Show Label", showLabelCheck);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetBoolLedShowLabel(showLabelCheck->isChecked());
            return;
        }

        if (m_widgetType == "bool.checkbox")
        {
            QDialog dialog(this);
            dialog.setWindowTitle("Checkbox Properties");

            auto* form = new QFormLayout(&dialog);
            auto* showLabelCheck = new QCheckBox(&dialog);
            showLabelCheck->setChecked(m_boolCheckboxShowLabel);
            form->addRow("Show Label", showLabelCheck);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);

            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted)
            {
                return;
            }

            SetBoolCheckboxShowLabel(showLabelCheck->isChecked());
            return;
        }

        if (!IsLinePlotWidget())
        {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle("Line Plot Properties");

        auto* form = new QFormLayout(&dialog);

        auto* bufferSizeSpin = new QSpinBox(&dialog);
        bufferSizeSpin->setRange(2, 500000);
        bufferSizeSpin->setValue(m_linePlotBufferSizeSamples);

        auto* autoYAxisCheck = new QCheckBox(&dialog);
        autoYAxisCheck->setChecked(m_linePlotAutoYAxis);

        auto* numberLinesCheck = new QCheckBox(&dialog);
        numberLinesCheck->setChecked(m_linePlotShowNumberLines);

        auto* gridLinesCheck = new QCheckBox(&dialog);
        gridLinesCheck->setChecked(m_linePlotShowGridLines);

        auto* upperLimitSpin = new QDoubleSpinBox(&dialog);
        upperLimitSpin->setDecimals(3);
        upperLimitSpin->setRange(-1e6, 1e6);
        upperLimitSpin->setValue(m_linePlotYUpperLimit);

        auto* lowerLimitSpin = new QDoubleSpinBox(&dialog);
        lowerLimitSpin->setDecimals(3);
        lowerLimitSpin->setRange(-1e6, 1e6);
        lowerLimitSpin->setValue(m_linePlotYLowerLimit);

        auto updateAxisSpinState = [autoYAxisCheck, lowerLimitSpin, upperLimitSpin]()
        {
            const bool manualEnabled = !autoYAxisCheck->isChecked();
            lowerLimitSpin->setEnabled(manualEnabled);
            upperLimitSpin->setEnabled(manualEnabled);
        };
        updateAxisSpinState();
        connect(autoYAxisCheck, &QCheckBox::toggled, &dialog, updateAxisSpinState);

        form->addRow("Buffer Size (samples)", bufferSizeSpin);
        form->addRow("Auto Y Axis", autoYAxisCheck);
        form->addRow("Show Number Lines", numberLinesCheck);
        form->addRow("Show Grid Lines", gridLinesCheck);
        form->addRow("Upper Limit", upperLimitSpin);
        form->addRow("Lower Limit", lowerLimitSpin);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }

        SetLinePlotProperties(
            bufferSizeSpin->value(),
            autoYAxisCheck->isChecked(),
            lowerLimitSpin->value(),
            upperLimitSpin->value()
        );
        SetLinePlotNumberLinesVisible(numberLinesCheck->isChecked());
        SetLinePlotGridLinesVisible(gridLinesCheck->isChecked());
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

    void VariableTile::ApplyProgressBarSettings()
    {
        if (m_progressBar == nullptr)
        {
            return;
        }

        m_progressBar->setTextVisible(m_progressBarShowPercentage);
        m_progressBar->setStyleSheet(
            "QProgressBar {"
            " border: 1px solid #4b4b4b;"
            " border-radius: 2px;"
            " background: " + m_progressBarBackgroundColor + ";"
            " padding: 0px;"
            " margin: 0px;"
            " min-height: 8px;"
            "}"
            "QProgressBar::chunk {"
            " background-color: " + m_progressBarForegroundColor + ";"
            " margin: 0px;"
            "}"
        );
    }

    void VariableTile::ApplySliderSettings()
    {
        if (m_controlWidget == nullptr)
        {
            return;
        }

        m_controlWidget->SetDoubleRange(m_sliderLowerLimit, m_sliderUpperLimit);
        m_controlWidget->SetDoubleTickSettings(m_sliderTickInterval, m_sliderShowTickMarks);
    }

    void VariableTile::ApplyLinePlotSettings()
    {
        if (m_linePlot == nullptr)
        {
            return;
        }

        m_linePlot->SetBufferSizeSamples(m_linePlotBufferSizeSamples);
        m_linePlot->SetYAxisModeAuto(m_linePlotAutoYAxis);
        m_linePlot->SetYAxisLimits(m_linePlotYLowerLimit, m_linePlotYUpperLimit);
        m_linePlot->SetShowNumberLines(m_linePlotShowNumberLines);
        m_linePlot->SetShowGridLines(m_linePlotShowGridLines);
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
