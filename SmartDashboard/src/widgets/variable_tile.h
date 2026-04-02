#pragma once

#include <QFrame>
#include <QSize>
#include <QStringList>

class QLabel;
class QMenu;
class QProgressBar;
class QDial;
class QGridLayout;
class QFrame;
class QKeyEvent;
class QLineEdit;
class QPaintEvent;
class QEnterEvent;
class QEvent;

namespace sd::widgets
{
    class TileControlWidget;
    class LinePlotWidget;
}

namespace sd::widgets
{
    enum class VariableType
    {
        Bool,
        Double,
        String
    };

    enum class EditInteractionMode
    {
        MoveOnly,
        ResizeOnly,
        MoveAndResize
    };

    enum class ValueOrigin
    {
        None,
        TemporaryDefault,
        RememberedControl,
        LiveTransport,
        LocalEdit
    };

    class VariableTile final : public QFrame
    {
        Q_OBJECT

    public:
        explicit VariableTile(const QString& key, VariableType type, QWidget* parent = nullptr);

        void SetEditable(bool editable);
        void ClearValue();
        void SetBoolValue(bool value);
        void SetDoubleValue(double value);
        void SetStringValue(const QString& value);
        void SetTemporaryDefaultBoolValue(bool value);
        void SetTemporaryDefaultDoubleValue(double value);
        void SetTemporaryDefaultStringValue(const QString& value);
        void SetShowEditHandles(bool showHandles);
        void SetSnapToGrid(bool enabled, int gridSize = 8);
        void SetEditInteractionMode(EditInteractionMode mode);
        void SetDefaultSize(const QSize& size);
        void SetGaugeProperties(double lowerLimit, double upperLimit, double tickInterval, bool showTickMarks);
        void SetProgressBarProperties(double lowerLimit, double upperLimit);
        void SetProgressBarShowPercentage(bool showPercentage);
        void SetProgressBarColors(const QString& foregroundColor, const QString& backgroundColor);
        void SetSliderProperties(double lowerLimit, double upperLimit, double tickInterval, bool showTickMarks);
        void SetLinePlotProperties(int bufferSizeSamples, bool autoYAxis, double yLowerLimit, double yUpperLimit);
        void SetLinePlotNumberLinesVisible(bool visible);
        void SetLinePlotGridLinesVisible(bool visible);
        void SetTextFontPointSize(int pointSize);
        void SetDoubleNumericEditable(bool editable);
        void SetShowLabel(bool showLabel);
        void SetSelected(bool selected);
        bool IsSelected() const;
        void SetStringChooserMode(bool chooserMode);
        void SetStringChooserOptions(const QStringList& options);
        bool IsLinePlotWidget() const;
        void ResetLinePlotGraph();

        QString GetKey() const;
        VariableType GetType() const;
        QString GetWidgetType() const;
        bool HasValue() const;
        bool HasLiveValue() const;
        bool IsShowingTemporaryDefault() const;
        ValueOrigin GetValueOrigin() const;
        bool GetBoolValue() const;
        double GetDoubleValue() const;
        QString GetStringValue() const;
        bool GetStringChooserMode() const;
        QStringList GetStringChooserOptions() const;
        void SetWidgetType(const QString& widgetType);
        void SetTitleText(const QString& title);

    signals:
        void ChangeWidgetRequested(const QString& key, const QString& widgetType);
        void RemoveRequested(const QString& key);
        void HideRequested(const QString& key);
        void ControlBoolEdited(const QString& key, bool value);
        void ControlDoubleEdited(const QString& key, double value);
        void ControlStringEdited(const QString& key, const QString& value);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void contextMenuEvent(QContextMenuEvent* event) override;

    private:
        enum class DragMode
        {
            None,
            Move,
            ResizeLeft,
            ResizeRight,
            ResizeTop,
            ResizeBottom,
            ResizeTopLeft,
            ResizeTopRight,
            ResizeBottomLeft,
            ResizeBottomRight
        };

        void BuildContextMenu(QMenu& menu);
        QString FormatValueText() const;
        void UpdateWidgetPresentation();
        void UpdateValueDisplay();
        void SetBoolValueInternal(bool value, ValueOrigin origin);
        void SetDoubleValueInternal(double value, ValueOrigin origin);
        void SetStringValueInternal(const QString& value, ValueOrigin origin);
        int DoubleToPercent(double value) const;
        int ValueToPercentForProgressBar(double value) const;
        void UpdateBoolLedAppearance();
        bool IsGaugeWidget() const;
        bool IsDoubleNumericWidget() const;
        bool IsPropertiesSupported() const;
        void OpenPropertiesDialog();
        void ApplyGaugeSettings();
        void ApplyProgressBarSettings();
        void ApplySliderSettings();
        void ApplyLinePlotSettings();
        DragMode HitTestDragMode(const QPoint& localPos) const;
        void UpdateCursorForPosition(const QPoint& localPos);

        QString m_key;
        VariableType m_type;
        QString m_widgetType;
        bool m_editable = false;
        bool m_showEditHandles = true;
        bool m_snapToGrid = true;
        int m_gridSize = 8;
        EditInteractionMode m_editInteractionMode = EditInteractionMode::MoveAndResize;
        bool m_isHovering = false;
        QPoint m_dragOrigin;
        QPoint m_dragStartGlobal;
        QRect m_dragStartGeometry;
        DragMode m_dragMode = DragMode::None;
        bool m_hasValue = false;
        ValueOrigin m_valueOrigin = ValueOrigin::None;
        bool m_boolValue = false;
        double m_doubleValue = 0.0;
        bool m_settingGaugeProgrammatically = false;
        bool m_settingDoubleEditProgrammatically = false;
        QSize m_defaultSize;
        bool m_doubleNumericEditable = false;
        int m_textFontPointSize = 0;
        bool m_showLabel = true;
        bool m_selected = false;
        int m_widthBeforeCompact = 0;
        bool m_stringChooserMode = false;
        QStringList m_stringChooserOptions;
        double m_gaugeLowerLimit = -1.0;
        double m_gaugeUpperLimit = 1.0;
        double m_gaugeTickInterval = 0.2;
        bool m_gaugeShowTickMarks = true;
        double m_progressBarLowerLimit = 0.0;
        double m_progressBarUpperLimit = 1.0;
        bool m_progressBarShowPercentage = false;
        QString m_progressBarForegroundColor = "#2e86de";
        QString m_progressBarBackgroundColor = "#1f1f1f";
        double m_sliderLowerLimit = -1.0;
        double m_sliderUpperLimit = 1.0;
        double m_sliderTickInterval = 0.2;
        bool m_sliderShowTickMarks = true;
        int m_linePlotBufferSizeSamples = 5000;
        bool m_linePlotAutoYAxis = true;
        bool m_linePlotShowNumberLines = false;
        bool m_linePlotShowGridLines = false;
        double m_linePlotYLowerLimit = 0.0;
        double m_linePlotYUpperLimit = 1.0;
        QString m_stringValue;

        QLabel* m_titleLabel = nullptr;
        QLabel* m_valueLabel = nullptr;
        QLineEdit* m_doubleEdit = nullptr;
        QProgressBar* m_progressBar = nullptr;
        QDial* m_gauge = nullptr;
        LinePlotWidget* m_linePlot = nullptr;
        QFrame* m_boolLed = nullptr;
        QGridLayout* m_layout = nullptr;
        TileControlWidget* m_controlWidget = nullptr;
    };
}
