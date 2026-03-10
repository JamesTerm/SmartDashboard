#include "widgets/variable_tile.h"

#include <QApplication>
#include <QProgressBar>

#include <gtest/gtest.h>

namespace
{
    QProgressBar* FindProgressBar(sd::widgets::VariableTile& tile)
    {
        return tile.findChild<QProgressBar*>();
    }
}

TEST(VariableTileTests, ProgressBarZeroCentersBeforeWidgetIsShown)
{
    int argc = 0;
    char** argv = nullptr;
    QApplication app(argc, argv);

    sd::widgets::VariableTile tile("test.progress", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.progress");
    tile.SetProgressBarProperties(-1.0, 1.0);

    tile.SetDoubleValue(0.0);

    QProgressBar* progressBar = FindProgressBar(tile);
    ASSERT_NE(progressBar, nullptr);
    EXPECT_EQ(progressBar->value(), 50);
}
