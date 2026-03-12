#include "widgets/playback_timeline_widget.h"

#include <QApplication>

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

TEST(PlaybackTimelineWidgetTests, ClampsCursorAndWindowToDuration)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::PlaybackTimelineWidget timeline;
    timeline.resize(800, 40);
    timeline.SetDurationUs(10'000'000);

    timeline.SetCursorUs(20'000'000);
    EXPECT_EQ(timeline.GetCursorUs(), 10'000'000);

    timeline.SetWindowUs(-2'000'000, 4'000'000);
    EXPECT_EQ(timeline.GetWindowStartUs(), 0);
    EXPECT_EQ(timeline.GetWindowEndUs(), 6'000'000);

    timeline.SetWindowUs(9'500'000, 15'000'000);
    EXPECT_EQ(timeline.GetWindowEndUs(), 10'000'000);
    EXPECT_LT(timeline.GetWindowStartUs(), timeline.GetWindowEndUs());
}
