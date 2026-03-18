#pragma once

#include "widgets/variable_tile.h"

#include <QWidget>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QSlider;
class QLineEdit;

namespace sd::widgets
{
    class TileControlWidget final : public QWidget
    {
        Q_OBJECT

    public:
        explicit TileControlWidget(VariableType type, QWidget* parent = nullptr);

        void SetBoolValue(bool value);
        void SetDoubleValue(double value);
        void SetDoubleRange(double lowerLimit, double upperLimit);
        void SetDoubleTickSettings(double tickInterval, bool showTickMarks);
        void SetTextFontPointSize(int pointSize);
        void SetStringValue(const QString& value);
        void SetStringOptions(const QStringList& options);
        void SetStringChooserMode(bool chooserMode);

    signals:
        void BoolEdited(bool value);
        void DoubleEdited(double value);
        void StringEdited(const QString& value);

    private:
        VariableType m_type;
        bool m_settingProgrammatically = false;
        double m_doubleLowerLimit = -1.0;
        double m_doubleUpperLimit = 1.0;
        QCheckBox* m_checkBox = nullptr;
        QComboBox* m_comboBox = nullptr;
        QSlider* m_slider = nullptr;
        QLineEdit* m_lineEdit = nullptr;
        bool m_stringChooserMode = false;
    };
}
