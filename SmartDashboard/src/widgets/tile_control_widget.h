#pragma once

#include "widgets/variable_tile.h"

#include <QWidget>

class QCheckBox;
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
        void SetStringValue(const QString& value);

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
        QSlider* m_slider = nullptr;
        QLineEdit* m_lineEdit = nullptr;
    };
}
