#include "widgets/playback_timeline_widget.h"

#include <QApplication>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    // Reuse one QApplication across tests to keep Qt test startup deterministic.
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

TEST(PlaybackTimelineWidgetTests, TickStepAdaptsAcrossZoomSpans)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::PlaybackTimelineWidget timeline;
    timeline.resize(800, 72);
    timeline.SetDurationUs(120'000'000);

    const std::int64_t fineStepUs = timeline.DebugComputeTickStepUs(2'000'000);
    const std::int64_t mediumStepUs = timeline.DebugComputeTickStepUs(12'000'000);
    const std::int64_t broadStepUs = timeline.DebugComputeTickStepUs(120'000'000);

    EXPECT_LT(fineStepUs, mediumStepUs);
    EXPECT_LT(mediumStepUs, broadStepUs);
}

TEST(PlaybackTimelineWidgetTests, TimeAndSpanLabelsUseReadableFormats)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::PlaybackTimelineWidget timeline;
    timeline.resize(800, 72);
    timeline.SetDurationUs(180'000'000);

    EXPECT_EQ(timeline.DebugFormatTimeLabel(0), QString("0.000s"));
    EXPECT_EQ(timeline.DebugFormatTimeLabel(12'345'000), QString("12.345s"));
    EXPECT_EQ(timeline.DebugFormatTimeLabel(62'345'000), QString("1:02.345"));
    EXPECT_EQ(timeline.DebugFormatSpanLabel(2'500'000), QString("2.500s"));
}
