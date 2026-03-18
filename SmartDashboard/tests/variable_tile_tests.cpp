#include "widgets/variable_tile.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QProgressBar>

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

    QProgressBar* FindProgressBar(sd::widgets::VariableTile& tile)
    {
        return tile.findChild<QProgressBar*>();
    }

    QComboBox* FindComboBox(sd::widgets::VariableTile& tile)
    {
        return tile.findChild<QComboBox*>();
    }

    QLineEdit* FindLineEdit(sd::widgets::VariableTile& tile)
    {
        return tile.findChild<QLineEdit*>();
    }
}

TEST(VariableTileTests, ProgressBarZeroCentersBeforeWidgetIsShown)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.progress", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.progress");
    tile.SetProgressBarProperties(-1.0, 1.0);

    tile.SetDoubleValue(0.0);

    QProgressBar* progressBar = FindProgressBar(tile);
    ASSERT_NE(progressBar, nullptr);
    EXPECT_EQ(progressBar->value(), 50);
}

TEST(VariableTileTests, StringChooserWidgetUsesComboAndTracksOptions)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.chooser", sd::widgets::VariableType::String);
    tile.SetWidgetType("string.chooser");
    tile.SetStringChooserMode(true);
    tile.SetStringChooserOptions(QStringList{ "DoNothing", "Taxi", "TwoPiece" });
    tile.SetStringValue("Taxi");

    QComboBox* comboBox = FindComboBox(tile);
    QLineEdit* lineEdit = FindLineEdit(tile);
    ASSERT_NE(comboBox, nullptr);
    ASSERT_NE(lineEdit, nullptr);
    EXPECT_FALSE(comboBox->isHidden());
    EXPECT_TRUE(lineEdit->isHidden());
    EXPECT_EQ(comboBox->count(), 3);
    EXPECT_EQ(comboBox->currentText(), QString("Taxi"));
}
