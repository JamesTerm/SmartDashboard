#include "widgets/variable_tile.h"

#include "widgets/line_plot_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
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

    sd::widgets::LinePlotWidget* FindLinePlot(sd::widgets::VariableTile& tile)
    {
        const QList<QWidget*> widgets = tile.findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (auto* plot = dynamic_cast<sd::widgets::LinePlotWidget*>(widget))
            {
                return plot;
            }
        }

        return nullptr;
    }

    QLabel* FindLabelWithText(sd::widgets::VariableTile& tile, const QString& text)
    {
        const QList<QLabel*> labels = tile.findChildren<QLabel*>();
        for (QLabel* label : labels)
        {
            if (label != nullptr && label->text() == text)
            {
                return label;
            }
        }

        return nullptr;
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

TEST(VariableTileTests, TilesShowPlaceholderUntilFirstValueArrives)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.numeric", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.numeric");

    QLabel* placeholder = FindLabelWithText(tile, "No data");
    ASSERT_NE(placeholder, nullptr);
    EXPECT_FALSE(tile.HasValue());

    tile.SetDoubleValue(4.25);

    EXPECT_TRUE(tile.HasValue());
    EXPECT_EQ(FindLabelWithText(tile, "No data"), nullptr);
}

TEST(VariableTileTests, TemporaryDefaultYieldsToFirstLiveValue)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.slider", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.slider");

    tile.SetTemporaryDefaultDoubleValue(0.0);

    EXPECT_TRUE(tile.HasValue());
    EXPECT_FALSE(tile.HasLiveValue());
    EXPECT_TRUE(tile.IsShowingTemporaryDefault());
    EXPECT_EQ(tile.GetDoubleValue(), 0.0);

    tile.SetDoubleValue(4.25);

    EXPECT_TRUE(tile.HasValue());
    EXPECT_TRUE(tile.HasLiveValue());
    EXPECT_FALSE(tile.IsShowingTemporaryDefault());
    EXPECT_EQ(tile.GetDoubleValue(), 4.25);
}

TEST(VariableTileTests, TemporaryDefaultBoolAndStringStatesAreRepresentable)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile boolTile("test.bool", sd::widgets::VariableType::Bool);
    boolTile.SetWidgetType("bool.led");
    boolTile.SetTemporaryDefaultBoolValue(false);

    EXPECT_TRUE(boolTile.HasValue());
    EXPECT_FALSE(boolTile.HasLiveValue());
    EXPECT_TRUE(boolTile.IsShowingTemporaryDefault());
    EXPECT_FALSE(boolTile.GetBoolValue());

    sd::widgets::VariableTile chooserTile("test.chooser.default", sd::widgets::VariableType::String);
    chooserTile.SetWidgetType("string.chooser");
    chooserTile.SetStringChooserMode(true);
    chooserTile.SetStringChooserOptions(QStringList{ "Move_Forward", "Taxi" });
    chooserTile.SetTemporaryDefaultStringValue("Move_Forward");

    EXPECT_TRUE(chooserTile.HasValue());
    EXPECT_FALSE(chooserTile.HasLiveValue());
    EXPECT_TRUE(chooserTile.IsShowingTemporaryDefault());
    EXPECT_EQ(chooserTile.GetStringValue(), QString("Move_Forward"));
}

TEST(VariableTileTests, EmptyTemporaryStringDefaultSuppressesNoDataPlaceholder)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.string", sd::widgets::VariableType::String);
    tile.SetWidgetType("string.text");
    tile.SetTemporaryDefaultStringValue(QString());

    EXPECT_TRUE(tile.HasValue());
    EXPECT_FALSE(tile.HasLiveValue());
    EXPECT_TRUE(tile.IsShowingTemporaryDefault());
    EXPECT_EQ(tile.GetStringValue(), QString());
    EXPECT_EQ(FindLabelWithText(tile, "No data"), nullptr);
}

TEST(VariableTileTests, ResetLinePlotGraphClearsAccumulatedSamples)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.lineplot", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.lineplot");
    tile.SetDoubleValue(1.25);
    tile.SetDoubleValue(2.5);

    sd::widgets::LinePlotWidget* plot = FindLinePlot(tile);
    ASSERT_NE(plot, nullptr);
    EXPECT_EQ(plot->GetSampleCountForTesting(), 2);

    tile.ResetLinePlotGraph();

    EXPECT_EQ(plot->GetSampleCountForTesting(), 0);
}
