#include "widgets/tile_control_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSlider>

namespace sd::widgets
{
    TileControlWidget::TileControlWidget(VariableType type, QWidget* parent)
        : QWidget(parent)
        , m_type(type)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        if (m_type == VariableType::Bool)
        {
            m_checkBox = new QCheckBox(this);
            layout->addWidget(m_checkBox);
            connect(m_checkBox, &QCheckBox::toggled, this, [this](bool checked)
            {
                if (!m_settingProgrammatically)
                {
                    emit BoolEdited(checked);
                }
            });
        }
        else if (m_type == VariableType::Double)
        {
            m_slider = new QSlider(Qt::Horizontal, this);
            m_slider->setRange(0, 100);
            m_slider->setTickPosition(QSlider::TicksBelow);
            layout->addWidget(m_slider);
            connect(m_slider, &QSlider::valueChanged, this, [this](int raw)
            {
                if (!m_settingProgrammatically)
                {
                    const double normalized = static_cast<double>(raw) / 100.0;
                    emit DoubleEdited(m_doubleLowerLimit + (normalized * (m_doubleUpperLimit - m_doubleLowerLimit)));
                }
            });
        }
        else if (m_type == VariableType::String)
        {
            m_lineEdit = new QLineEdit(this);
            m_comboBox = new QComboBox(this);
            m_comboBox->setVisible(false);
            layout->addWidget(m_lineEdit);
            layout->addWidget(m_comboBox);

            connect(m_lineEdit, &QLineEdit::editingFinished, this, [this]()
            {
                if (!m_settingProgrammatically && !m_stringChooserMode)
                {
                    emit StringEdited(m_lineEdit->text());
                }
            });

            connect(m_comboBox, &QComboBox::currentTextChanged, this, [this](const QString& text)
            {
                if (m_settingProgrammatically || !m_stringChooserMode)
                {
                    return;
                }

                emit StringEdited(text);
            });
        }
    }

    void TileControlWidget::SetBoolValue(bool value)
    {
        if (!m_checkBox)
        {
            return;
        }

        m_settingProgrammatically = true;
        m_checkBox->setChecked(value);
        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetDoubleValue(double value)
    {
        if (!m_slider)
        {
            return;
        }

        double clamped = value;
        if (clamped < m_doubleLowerLimit)
        {
            clamped = m_doubleLowerLimit;
        }
        else if (clamped > m_doubleUpperLimit)
        {
            clamped = m_doubleUpperLimit;
        }

        const double span = m_doubleUpperLimit - m_doubleLowerLimit;
        const double normalized = (span <= 0.0) ? 0.0 : (clamped - m_doubleLowerLimit) / span;

        m_settingProgrammatically = true;
        m_slider->setValue(static_cast<int>(normalized * 100.0 + 0.5));
        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetDoubleRange(double lowerLimit, double upperLimit)
    {
        double lower = lowerLimit;
        double upper = upperLimit;
        if (upper <= lower)
        {
            upper = lower + 0.001;
        }

        m_doubleLowerLimit = lower;
        m_doubleUpperLimit = upper;
    }

    void TileControlWidget::SetDoubleTickSettings(double tickInterval, bool showTickMarks)
    {
        if (!m_slider)
        {
            return;
        }

        m_slider->setTickPosition(showTickMarks ? QSlider::TicksBelow : QSlider::NoTicks);

        const double span = m_doubleUpperLimit - m_doubleLowerLimit;
        int tickStep = 1;
        if (span > 0.0 && tickInterval > 0.0)
        {
            tickStep = static_cast<int>((tickInterval / span) * 100.0 + 0.5);
            if (tickStep < 1)
            {
                tickStep = 1;
            }
            if (tickStep > 100)
            {
                tickStep = 100;
            }
        }

        m_slider->setTickInterval(tickStep);
        m_slider->setSingleStep(tickStep);
        m_slider->setPageStep(tickStep);
    }

    void TileControlWidget::SetStringValue(const QString& value)
    {
        if (!m_lineEdit || !m_comboBox)
        {
            return;
        }

        m_settingProgrammatically = true;
        m_lineEdit->setText(value);

        const int existingIndex = m_comboBox->findText(value);
        if (existingIndex >= 0)
        {
            m_comboBox->setCurrentIndex(existingIndex);
        }
        else if (!value.isEmpty())
        {
            m_comboBox->setCurrentText(value);
        }

        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetStringOptions(const QStringList& options)
    {
        if (!m_comboBox)
        {
            return;
        }

        m_settingProgrammatically = true;
        const QString previous = m_comboBox->currentText();
        m_comboBox->clear();
        m_comboBox->addItems(options);

        if (!previous.isEmpty())
        {
            const int idx = m_comboBox->findText(previous);
            if (idx >= 0)
            {
                m_comboBox->setCurrentIndex(idx);
            }
            else
            {
                m_comboBox->setCurrentText(previous);
            }
        }
        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetStringChooserMode(bool chooserMode)
    {
        m_stringChooserMode = chooserMode;

        if (m_lineEdit != nullptr)
        {
            m_lineEdit->setVisible(!m_stringChooserMode);
            m_lineEdit->setEnabled(!m_stringChooserMode);
        }

        if (m_comboBox != nullptr)
        {
            m_comboBox->setVisible(m_stringChooserMode);
            m_comboBox->setEnabled(m_stringChooserMode);
        }
    }

    void TileControlWidget::SetTextFontPointSize(int pointSize)
    {
        QFont font = this->font();
        if (pointSize > 0)
        {
            font.setPointSize(pointSize);
        }

        if (m_lineEdit != nullptr)
        {
            m_lineEdit->setFont(font);
        }

        if (m_comboBox != nullptr)
        {
            m_comboBox->setFont(font);
        }

        if (m_checkBox != nullptr)
        {
            m_checkBox->setFont(font);
        }
    }
}
