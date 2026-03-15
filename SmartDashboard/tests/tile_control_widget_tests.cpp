#include "widgets/tile_control_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    QApplication* EnsureApp()
    {
        if (QApplication::instance() != nullptr)
        {
            return qobject_cast<QApplication*>(QApplication::instance());
        }

        static int argc = 1;
        static char appName[] = "SmartDashboardTests";
        static char* argv[] = { appName };
        static std::unique_ptr<QApplication> app = std::make_unique<QApplication>(argc, argv);
        return app.get();
    }
}

TEST(TileControlWidgetTests, StringChooserModeTogglesEditorAndCombo)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::TileControlWidget control(sd::widgets::VariableType::String);

    QLineEdit* lineEdit = control.findChild<QLineEdit*>();
    QComboBox* comboBox = control.findChild<QComboBox*>();
    ASSERT_NE(lineEdit, nullptr);
    ASSERT_NE(comboBox, nullptr);

    control.SetStringChooserMode(false);
    EXPECT_FALSE(lineEdit->isHidden());
    EXPECT_TRUE(comboBox->isHidden());

    control.SetStringChooserMode(true);
    EXPECT_TRUE(lineEdit->isHidden());
    EXPECT_FALSE(comboBox->isHidden());
}

TEST(TileControlWidgetTests, StringChooserOptionsAndValueStayInSync)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::TileControlWidget control(sd::widgets::VariableType::String);
    control.SetStringChooserMode(true);

    const QStringList options = { "DoNothing", "Taxi", "TwoPiece" };
    control.SetStringOptions(options);
    control.SetStringValue("Taxi");

    QComboBox* comboBox = control.findChild<QComboBox*>();
    ASSERT_NE(comboBox, nullptr);
    EXPECT_EQ(comboBox->count(), 3);
    EXPECT_EQ(comboBox->currentText(), QString("Taxi"));
}
