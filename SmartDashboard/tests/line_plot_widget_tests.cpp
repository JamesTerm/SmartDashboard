#include "widgets/line_plot_widget.h"

#include <QApplication>
#include <QThread>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

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

    void PumpSamples(sd::widgets::LinePlotWidget& plot, int sampleCount, int sleepMillis, double startValue)
    {
        for (int i = 0; i < sampleCount; ++i)
        {
            plot.AddSample(startValue + static_cast<double>(i));
            if (sleepMillis > 0)
            {
                QThread::msleep(static_cast<unsigned long>(sleepMillis));
            }
        }
    }

    void VerifyStableWindowForScenario(
        sd::widgets::LinePlotWidget& plot,
        int bufferSize,
        int sampleCount,
        int sleepMillis,
        double startValue)
    {
        plot.ResetGraph();
        plot.SetBufferSizeSamples(bufferSize);
        PumpSamples(plot, sampleCount, sleepMillis, startValue);

        const int expectedCount = std::min(bufferSize, sampleCount);
        EXPECT_EQ(plot.GetSampleCountForTesting(), expectedCount);

        const auto xRange = plot.GetXRangeForTesting();
        const double span = xRange.second - xRange.first;
        const double oldest = plot.GetOldestSampleTimeForTesting();
        const double latest = plot.GetLatestSampleTimeForTesting();

        EXPECT_NEAR(xRange.first, oldest, 1e-9);
        EXPECT_NEAR(xRange.second, latest, 1e-9);
        EXPECT_GT(span, 0.0);
        EXPECT_NEAR(span, latest - oldest, 1e-9);
    }
}

TEST(LinePlotWidgetTests, BufferSizeSetterMaintainsStabilityAcrossRates)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::LinePlotWidget plot;

    VerifyStableWindowForScenario(plot, 250, 250, 1, 0.0);
    VerifyStableWindowForScenario(plot, 250, 500, 1, 1000.0);
    VerifyStableWindowForScenario(plot, 1000, 1000, 0, 2000.0);
    VerifyStableWindowForScenario(plot, 120, 360, 2, 3000.0);
}

TEST(LinePlotWidgetTests, XTickIntervalRemainsStableUnderJitteredCadence)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::LinePlotWidget plot;
    plot.SetBufferSizeSamples(250);
    plot.SetShowNumberLines(true);

    constexpr int drawWidth = 900;
    std::vector<double> observedIntervals;
    observedIntervals.reserve(180);

    // Fill the buffer first so we measure tick stability during steady-state
    // scrolling, not the initial range-growth phase.
    for (int i = 0; i < 250; ++i)
    {
        plot.AddSample(std::sin(static_cast<double>(i) * 0.08));
        QThread::msleep(16);
    }

    const int cadencePatternMs[] = { 16, 17, 15, 16, 18, 14, 16, 16 };
    for (int i = 0; i < 180; ++i)
    {
        const double value = std::sin(static_cast<double>(i + 250) * 0.08);
        plot.AddSample(value);
        observedIntervals.push_back(plot.GetXTickIntervalForTesting(drawWidth));
        QThread::msleep(static_cast<unsigned long>(cadencePatternMs[i % 8]));
    }

    int tickSwitches = 0;
    for (size_t i = 1; i < observedIntervals.size(); ++i)
    {
        if (std::abs(observedIntervals[i] - observedIntervals[i - 1]) > 1e-9)
        {
            ++tickSwitches;
        }
    }

    EXPECT_LE(tickSwitches, 2);
}

TEST(LinePlotWidgetTests, XRangeAnchorsToOldestAndNewestDuringBurstPauseResume)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::LinePlotWidget plot;
    plot.SetBufferSizeSamples(250);

    for (int i = 0; i < 250; ++i)
    {
        plot.AddSample(std::sin(static_cast<double>(i) * 0.05));
        QThread::msleep(16);
    }

    const auto beforePauseRange = plot.GetXRangeForTesting();
    const double beforePauseOldest = plot.GetOldestSampleTimeForTesting();
    const double beforePauseLatest = plot.GetLatestSampleTimeForTesting();
    EXPECT_NEAR(beforePauseRange.first, beforePauseOldest, 1e-9);
    EXPECT_NEAR(beforePauseRange.second, beforePauseLatest, 1e-9);

    QThread::msleep(400);

    for (int i = 0; i < 30; ++i)
    {
        plot.AddSample(std::cos(static_cast<double>(i) * 0.07));
        QThread::msleep(16);
    }

    const auto afterResumeRange = plot.GetXRangeForTesting();
    const double afterResumeOldest = plot.GetOldestSampleTimeForTesting();
    const double afterResumeLatest = plot.GetLatestSampleTimeForTesting();

    EXPECT_EQ(plot.GetSampleCountForTesting(), 250);
    EXPECT_NEAR(afterResumeRange.first, afterResumeOldest, 1e-9);
    EXPECT_NEAR(afterResumeRange.second, afterResumeLatest, 1e-9);
    EXPECT_GT(afterResumeRange.second - afterResumeRange.first, 0.001);
}
