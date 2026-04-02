#include "app/main_window.h"
#include "layout/layout_serializer.h"
#include "widgets/run_browser_dock.h"

#include "sd_direct_types.h"

#include <QApplication>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    struct RememberedControlEntry
    {
        QString key;
        int valueType = 0;
        QVariant value;
    };

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

    std::vector<RememberedControlEntry> ReadRememberedControls()
    {
        std::vector<RememberedControlEntry> entries;

        QSettings settings("SmartDashboard", "SmartDashboardApp");
        const int size = settings.beginReadArray("directRememberedControls");
        entries.reserve(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
        {
            settings.setArrayIndex(i);

            RememberedControlEntry entry;
            entry.key = settings.value("key").toString();
            entry.valueType = settings.value("valueType").toInt();
            entry.value = settings.value("value");
            entries.push_back(entry);
        }
        settings.endArray();

        return entries;
    }

    void WriteRememberedControls(const std::vector<RememberedControlEntry>& entries)
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.remove("directRememberedControls");
        settings.beginWriteArray("directRememberedControls");
        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            settings.setArrayIndex(i);
            settings.setValue("key", entries[static_cast<std::size_t>(i)].key);
            settings.setValue("valueType", entries[static_cast<std::size_t>(i)].valueType);
            settings.setValue("value", entries[static_cast<std::size_t>(i)].value);
        }
        settings.endArray();
        settings.sync();
    }

    class ScopedPersistenceSettings final
    {
    public:
        ScopedPersistenceSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            m_hadTransportKind = settings.contains("connection/transportKind");
            m_transportKind = settings.value("connection/transportKind");
            m_hadTransportId = settings.contains("connection/transportId");
            m_transportId = settings.value("connection/transportId");
            m_rememberedControls = ReadRememberedControls();
        }

        ~ScopedPersistenceSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            if (m_hadTransportKind)
            {
                settings.setValue("connection/transportKind", m_transportKind);
            }
            else
            {
                settings.remove("connection/transportKind");
            }

            if (m_hadTransportId)
            {
                settings.setValue("connection/transportId", m_transportId);
            }
            else
            {
                settings.remove("connection/transportId");
            }

            settings.sync();
            WriteRememberedControls(m_rememberedControls);
        }

    private:
        bool m_hadTransportKind = false;
        QVariant m_transportKind;
        bool m_hadTransportId = false;
        QVariant m_transportId;
        std::vector<RememberedControlEntry> m_rememberedControls;
    };

    void ClearRememberedControlsSetting()
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.remove("directRememberedControls");
        settings.sync();
    }

    int RememberedControlsSettingSize()
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        const int size = settings.beginReadArray("directRememberedControls");
        settings.endArray();
        return size;
    }

    QVariant RememberedControlSettingValue(const QString& key)
    {
        for (const RememberedControlEntry& entry : ReadRememberedControls())
        {
            if (entry.key == key)
            {
                return entry.value;
            }
        }

        return {};
    }

    void SetStartupTransport(const QString& transportId, sd::transport::TransportKind kind)
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.setValue("connection/transportId", transportId);
        settings.setValue("connection/transportKind", static_cast<int>(kind));
        settings.sync();
    }
}

TEST(MainWindowPersistenceTests, NativeLinkTelemetryUpdatesDoNotPopulateRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("native-link", sd::transport::TransportKind::Plugin);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("native-link", sd::transport::TransportKind::Plugin);

    window.SimulateVariableUpdateForTesting(
        "TestMove",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(5.14),
        1
    );
    window.SimulateVariableUpdateForTesting(
        "Timer",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(1797.4630771),
        2
    );
    window.SimulateVariableUpdateForTesting(
        "Test/Auton_Selection/AutoChooser/selected",
        static_cast<int>(sd::direct::ValueType::String),
        QVariant(QString("Just Move Forward")),
        3
    );

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 0);
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("Timer"));
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("Test/Auton_Selection/AutoChooser"));
    EXPECT_EQ(RememberedControlsSettingSize(), 0);
}

TEST(MainWindowPersistenceTests, DirectTelemetryUpdatesDoNotCreateRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);

    window.SimulateVariableUpdateForTesting(
        "TestMove",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(3.5),
        1
    );

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 0);
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_EQ(RememberedControlsSettingSize(), 0);
}

TEST(MainWindowPersistenceTests, DISABLED_DirectControlEditsStillPersistRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);

    window.SimulateControlDoubleEditForTesting("TestMove", 3.5);

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 1);
    EXPECT_TRUE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_EQ(RememberedControlsSettingSize(), 1);
    EXPECT_EQ(RememberedControlSettingValue("TestMove").toString(), QString("3.5"));
}

TEST(MainWindowPersistenceTests, DISABLED_DirectSliderEditUpdatesRememberedValueAcrossReopen)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    {
        MainWindow firstWindow(nullptr, false);
        firstWindow.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);
        firstWindow.SimulateControlDoubleEditForTesting("TestMove", 7.25);
    }

    EXPECT_EQ(RememberedControlsSettingSize(), 1);
    EXPECT_EQ(RememberedControlSettingValue("TestMove").toString(), QString("7.25"));

    MainWindow reopenedWindow(nullptr, false);
    EXPECT_TRUE(reopenedWindow.HasRememberedControlValueForTesting("TestMove"));
}

TEST(MainWindowPersistenceTests, RememberedControlsStayDisabledByDefaultEvenOnDirect)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;

    WriteRememberedControls({
        {
            QStringLiteral("TestMove"),
            static_cast<int>(sd::direct::ValueType::Double),
            QVariant(7.25)
        }
    });
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);
    window.LoadRememberedControlValuesForTesting();

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 0);
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("TestMove"));
}

TEST(MainWindowPersistenceTests, ClearWidgetsThenReloadLayoutReappliesTemporaryDefaults)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    const QString layoutPath = QDir::toNativeSeparators(QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).filePath("../../../../Robot_Simulation/Design/Swervelayout.json")
    ));

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(layoutPath, true, false));
    EXPECT_TRUE(window.TileHasValueForTesting("TestMove"));
    EXPECT_TRUE(window.TileIsTemporaryDefaultForTesting("TestMove"));
    EXPECT_TRUE(window.TileHasValueForTesting("Test/Auton_Selection/AutoChooser"));
    EXPECT_TRUE(window.TileIsTemporaryDefaultForTesting("Test/Auton_Selection/AutoChooser"));
}

TEST(MainWindowPersistenceTests, AutoConnectFalseSurvivesSyncToPluginSettingsJson)
{
    // Ian: "auto_connect" lives only in pluginSettingsJson (no dedicated
    // ConnectionConfig member). SyncConnectionConfigToPluginSettingsJson
    // rebuilds the JSON from scratch using the individual ConnectionConfig
    // fields; without explicit round-trip logic it would silently discard
    // "auto_connect": false written by SetConnectionFieldValue. This test pins
    // that the round-trip preserves the user's explicit false choice.
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    SetStartupTransport("native-link", sd::transport::TransportKind::Plugin);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("native-link", sd::transport::TransportKind::Plugin);

    // Write auto_connect=false into pluginSettingsJson.
    window.SetConnectionFieldValueForTesting(QStringLiteral("auto_connect"), false);

    // Rebuild pluginSettingsJson from the split ConnectionConfig fields --
    // this is the exact path taken by OnEditTransportSettings after the user
    // clicks OK in the settings dialog.
    window.SyncConnectionConfigToPluginSettingsJsonForTesting();

    // Read it back. The default is true (reconnect on) to match the plugin
    // fallback; if false is not preserved the call returns true and the
    // test correctly fails.
    EXPECT_FALSE(window.GetConnectionFieldBoolForTesting(QStringLiteral("auto_connect"), true))
        << "auto_connect=false must survive SyncConnectionConfigToPluginSettingsJson";
}

// ---------------------------------------------------------------------------
// Layout-persisted visibility (Feature 4)
// ---------------------------------------------------------------------------

namespace
{
    /// Write a layout JSON string to a temp file.
    QString WriteLayoutFixture(const QTemporaryDir& dir, const QString& fileName, const QByteArray& json)
    {
        const QString path = dir.filePath(fileName);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return {};
        }
        f.write(json);
        f.close();
        return path;
    }

    /// Clear Run Browser QSettings so stale hidden/checked keys from previous
    /// runs (or the real app on this machine) don't bleed into test results.
    void ClearRunBrowserSettings()
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.remove("runBrowser/active");
        settings.remove("runBrowser/checkedKeys");
        settings.remove("runBrowser/hiddenKeys");
        settings.remove("runBrowser/expandedPaths");
        settings.sync();
    }

    /// RAII helper: clears Run Browser settings on construction and restores
    /// the original values on destruction so tests are isolated.
    class ScopedRunBrowserSettings final
    {
    public:
        ScopedRunBrowserSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            m_active = settings.value("runBrowser/active");
            m_checkedKeys = settings.value("runBrowser/checkedKeys");
            m_hiddenKeys = settings.value("runBrowser/hiddenKeys");
            m_expandedPaths = settings.value("runBrowser/expandedPaths");
            ClearRunBrowserSettings();
        }

        ~ScopedRunBrowserSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            if (m_active.isValid()) settings.setValue("runBrowser/active", m_active);
            else settings.remove("runBrowser/active");
            if (m_checkedKeys.isValid()) settings.setValue("runBrowser/checkedKeys", m_checkedKeys);
            else settings.remove("runBrowser/checkedKeys");
            if (m_hiddenKeys.isValid()) settings.setValue("runBrowser/hiddenKeys", m_hiddenKeys);
            else settings.remove("runBrowser/hiddenKeys");
            if (m_expandedPaths.isValid()) settings.setValue("runBrowser/expandedPaths", m_expandedPaths);
            else settings.remove("runBrowser/expandedPaths");
            settings.sync();
        }

    private:
        QVariant m_active;
        QVariant m_checkedKeys;
        QVariant m_hiddenKeys;
        QVariant m_expandedPaths;
    };

    // Layout with two widgets and one hidden key.
    const QByteArray kLayoutWithHiddenKeys = R"({
        "version": 1,
        "widgets": [
            {
                "widgetId": "tile_speed",
                "variableKey": "Robot/Speed",
                "widgetType": "double.numeric",
                "geometry": { "x": 10, "y": 10, "w": 220, "h": 84 }
            },
            {
                "widgetId": "tile_heading",
                "variableKey": "Robot/Heading",
                "widgetType": "double.numeric",
                "geometry": { "x": 250, "y": 10, "w": 220, "h": 84 }
            }
        ],
        "hiddenKeys": [ "Robot/Heading" ]
    })";

    // Layout with two widgets and no hidden keys (backward compat).
    const QByteArray kLayoutWithoutHiddenKeys = R"({
        "version": 1,
        "widgets": [
            {
                "widgetId": "tile_speed",
                "variableKey": "Robot/Speed",
                "widgetType": "double.numeric",
                "geometry": { "x": 10, "y": 10, "w": 220, "h": 84 }
            },
            {
                "widgetId": "tile_heading",
                "variableKey": "Robot/Heading",
                "widgetType": "double.numeric",
                "geometry": { "x": 250, "y": 10, "w": 220, "h": 84 }
            }
        ]
    })";

    // Layout with both keys hidden.
    const QByteArray kLayoutWithAllHidden = R"({
        "version": 1,
        "widgets": [
            {
                "widgetId": "tile_speed",
                "variableKey": "Robot/Speed",
                "widgetType": "double.numeric",
                "geometry": { "x": 10, "y": 10, "w": 220, "h": 84 }
            },
            {
                "widgetId": "tile_heading",
                "variableKey": "Robot/Heading",
                "widgetType": "double.numeric",
                "geometry": { "x": 250, "y": 10, "w": 220, "h": 84 }
            }
        ],
        "hiddenKeys": [ "Robot/Speed", "Robot/Heading" ]
    })";
}

// -- Serializer-level tests --

TEST(LayoutSerializerHiddenKeysTests, LoadLayoutEntries_WithHiddenKeys_ReturnsHiddenKeys)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    QSet<QString> hiddenKeys;
    ASSERT_TRUE(sd::layout::LoadLayoutEntries(path, entries, &hiddenKeys));

    EXPECT_EQ(entries.size(), 2u);
    EXPECT_EQ(hiddenKeys.size(), 1);
    EXPECT_TRUE(hiddenKeys.contains("Robot/Heading"));
    EXPECT_FALSE(hiddenKeys.contains("Robot/Speed"));
}

TEST(LayoutSerializerHiddenKeysTests, LoadLayoutEntries_WithoutHiddenKeys_ReturnsEmptySet)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithoutHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    QSet<QString> hiddenKeys;
    ASSERT_TRUE(sd::layout::LoadLayoutEntries(path, entries, &hiddenKeys));

    EXPECT_EQ(entries.size(), 2u);
    EXPECT_TRUE(hiddenKeys.isEmpty());
}

TEST(LayoutSerializerHiddenKeysTests, LoadLayoutEntries_NullOutParam_DoesNotCrash)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    // Pass nullptr — should succeed without crash.
    ASSERT_TRUE(sd::layout::LoadLayoutEntries(path, entries, nullptr));
    EXPECT_EQ(entries.size(), 2u);
}

TEST(LayoutSerializerHiddenKeysTests, LoadLayoutEntries_EmptyHiddenKeysArray_ReturnsEmptySet)
{
    const QByteArray json = R"({
        "version": 1,
        "widgets": [],
        "hiddenKeys": []
    })";

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", json);
    ASSERT_FALSE(path.isEmpty());

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    QSet<QString> hiddenKeys;
    ASSERT_TRUE(sd::layout::LoadLayoutEntries(path, entries, &hiddenKeys));
    EXPECT_TRUE(hiddenKeys.isEmpty());
}

TEST(LayoutSerializerHiddenKeysTests, LoadLayoutEntries_HiddenKeysWithEmptyStrings_IgnoresEmpty)
{
    const QByteArray json = R"({
        "version": 1,
        "widgets": [],
        "hiddenKeys": [ "Robot/Speed", "", "Robot/Heading" ]
    })";

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", json);
    ASSERT_FALSE(path.isEmpty());

    std::vector<sd::layout::WidgetLayoutEntry> entries;
    QSet<QString> hiddenKeys;
    ASSERT_TRUE(sd::layout::LoadLayoutEntries(path, entries, &hiddenKeys));
    EXPECT_EQ(hiddenKeys.size(), 2);
    EXPECT_TRUE(hiddenKeys.contains("Robot/Speed"));
    EXPECT_TRUE(hiddenKeys.contains("Robot/Heading"));
}

// -- MainWindow integration tests --

TEST(LayoutHiddenKeysIntegrationTests, LoadLayoutWithHiddenKeys_HidesTiles)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(path, true, false));

    // Robot/Speed should be visible, Robot/Heading should be hidden.
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Speed"));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"));
}

TEST(LayoutHiddenKeysIntegrationTests, LoadLayoutWithoutHiddenKeys_ShowsAllTiles)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithoutHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(path, true, false));

    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Speed"));
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Heading"));
}

TEST(LayoutHiddenKeysIntegrationTests, LoadLayoutWithAllHidden_HidesAllTiles)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithAllHidden);
    ASSERT_FALSE(path.isEmpty());

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(path, true, false));

    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Speed"));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"));
}

TEST(LayoutHiddenKeysIntegrationTests, LoadLayoutWithHidden_ThenLoadWithout_ShowsAll)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString hiddenPath = WriteLayoutFixture(tempDir, "hidden.json", kLayoutWithHiddenKeys);
    const QString showAllPath = WriteLayoutFixture(tempDir, "showall.json", kLayoutWithoutHiddenKeys);
    ASSERT_FALSE(hiddenPath.isEmpty());
    ASSERT_FALSE(showAllPath.isEmpty());

    // First: load layout with one hidden key.
    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(hiddenPath, true, false));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"));

    // Second: load layout without hidden keys — all tiles should be visible.
    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(showAllPath, true, false));
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Speed"));
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Heading"));
}

TEST(LayoutHiddenKeysIntegrationTests, SaveLayoutRoundTrip_PersistsHiddenKeys)
{
    // Ian: Create tiles, hide one, save layout, reload and verify hidden state
    // round-trips through the JSON file.
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // Load a layout with one hidden key so tiles exist and one is hidden.
    const QString seedPath = WriteLayoutFixture(tempDir, "seed.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(seedPath.isEmpty());
    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(seedPath, true, false));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"));

    // Save the layout to a new file.
    const QString savePath = tempDir.filePath("saved.json");
    ASSERT_TRUE(window.SaveLayoutToPathForTesting(savePath));

    // Read the saved JSON and verify it contains hiddenKeys.
    QFile savedFile(savePath);
    ASSERT_TRUE(savedFile.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(savedFile.readAll());
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();
    ASSERT_TRUE(root.contains("hiddenKeys"));
    const QJsonArray hiddenArray = root.value("hiddenKeys").toArray();
    QSet<QString> savedHiddenKeys;
    for (const QJsonValue& val : hiddenArray)
    {
        savedHiddenKeys.insert(val.toString());
    }
    EXPECT_EQ(savedHiddenKeys.size(), 1);
    EXPECT_TRUE(savedHiddenKeys.contains("Robot/Heading"));
    EXPECT_FALSE(savedHiddenKeys.contains("Robot/Speed"));

    // Reload into a fresh window and verify tile visibility.
    MainWindow window2(nullptr, false);
    window2.ClearWidgetsForTesting();
    ASSERT_TRUE(window2.LoadLayoutFromPathForTesting(savePath, true, false));
    EXPECT_TRUE(window2.TileIsVisibleForTesting("Robot/Speed"));
    EXPECT_FALSE(window2.TileIsVisibleForTesting("Robot/Heading"));
}

TEST(LayoutHiddenKeysIntegrationTests, SaveLayoutWithNoHiddenTiles_OmitsHiddenKeysField)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // Load a layout with all tiles visible.
    const QString seedPath = WriteLayoutFixture(tempDir, "seed.json", kLayoutWithoutHiddenKeys);
    ASSERT_FALSE(seedPath.isEmpty());
    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(seedPath, true, false));

    // Save and check that hiddenKeys is absent from JSON.
    const QString savePath = tempDir.filePath("saved.json");
    ASSERT_TRUE(window.SaveLayoutToPathForTesting(savePath));

    QFile savedFile(savePath);
    ASSERT_TRUE(savedFile.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(savedFile.readAll());
    ASSERT_TRUE(doc.isObject());
    EXPECT_FALSE(doc.object().contains("hiddenKeys"))
        << "Layout with no hidden tiles should not contain hiddenKeys field";
}

// -- Hidden-state sovereignty tests --
// Ian: These tests verify that layout-persisted hidden state is never
// overridden by transport lifecycle events.  The rule: only explicit user
// action can change what is hidden.

TEST(LayoutHiddenKeysIntegrationTests, HiddenTilesSurviveVariableUpdateForExistingKey)
{
    // When a variable update arrives for a key that already has a hidden tile,
    // the tile must remain hidden.  This simulates what happens when the robot
    // connects and starts publishing values for tiles that the layout says
    // should be hidden.
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(path, true, false));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"))
        << "Tile should start hidden per layout";

    // Simulate the robot publishing an update for the hidden key.
    window.SimulateVariableUpdateForTesting("Robot/Heading", 1 /*double*/, QVariant(45.0));

    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"))
        << "Hidden tile must remain hidden after variable update";
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Speed"))
        << "Visible tile must remain visible after variable update";
}

TEST(LayoutHiddenKeysIntegrationTests, HiddenTilesSurviveVariableUpdateForNewKey)
{
    // When a variable update arrives for a key NOT in the layout, a new tile
    // is created visible (it's not in hiddenKeys).  Meanwhile the existing
    // hidden tile must stay hidden — the TileAdded -> OnTileAdded ->
    // CheckedSignalsChanged chain must not show all tiles.
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString path = WriteLayoutFixture(tempDir, "layout.json", kLayoutWithHiddenKeys);
    ASSERT_FALSE(path.isEmpty());

    ASSERT_TRUE(window.LoadLayoutFromPathForTesting(path, true, false));
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"))
        << "Tile should start hidden per layout";

    // Simulate the robot publishing a NEW key that doesn't exist in the layout.
    window.SimulateVariableUpdateForTesting("Robot/Battery", 1 /*double*/, QVariant(12.5));

    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Battery"))
        << "New tile (not in hiddenKeys) should be visible";
    EXPECT_FALSE(window.TileIsVisibleForTesting("Robot/Heading"))
        << "Hidden tile must remain hidden after new tile creation";
    EXPECT_TRUE(window.TileIsVisibleForTesting("Robot/Speed"))
        << "Visible tile must remain visible after new tile creation";
}

// -- Reconnect sovereignty test --
// Ian: This verifies the fix for the stale-m_runBrowserHiddenKeys bug.
// When a user hides tiles during a streaming session, then the transport
// disconnects and reconnects (auto-connect cycle), the hidden tiles must
// stay hidden.  The root cause was that m_runBrowserHiddenKeys was only
// loaded from QSettings at startup and never updated in memory when the
// user toggled visibility.  On reconnect, StartTransport() passed the
// stale startup value to SetHiddenDiscoveredKeys(), losing any runtime
// hide actions.

TEST(LayoutHiddenKeysIntegrationTests, HiddenTilesSurviveStreamingReconnect)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    const ScopedRunBrowserSettings scopedRunBrowser;
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.ClearWidgetsForTesting();

    // Create tiles by simulating variable updates.
    window.SimulateVariableUpdateForTesting("motor/RPM", 1 /*double*/, QVariant(1000.0));
    window.SimulateVariableUpdateForTesting("motor/Current", 1 /*double*/, QVariant(5.0));
    window.SimulateVariableUpdateForTesting("sensor/Temperature", 1 /*double*/, QVariant(42.0));

    // Simulate initial connection: puts the dock in streaming mode and
    // feeds existing tiles into it.
    window.SimulateStreamingReconnectForTesting();

    // All tiles should be visible initially.
    ASSERT_TRUE(window.TileIsVisibleForTesting("motor/RPM"));
    ASSERT_TRUE(window.TileIsVisibleForTesting("motor/Current"));
    ASSERT_TRUE(window.TileIsVisibleForTesting("sensor/Temperature"));

    // User hides motor/RPM via the Run Browser dock.
    sd::widgets::RunBrowserDock* dock = window.GetRunBrowserDockForTesting();
    ASSERT_NE(dock, nullptr);
    QSet<QString> keysToHide;
    keysToHide.insert("motor/RPM");
    dock->UncheckSignalsByKeys(keysToHide);

    ASSERT_FALSE(window.TileIsVisibleForTesting("motor/RPM"))
        << "motor/RPM should be hidden after user action";
    ASSERT_TRUE(window.TileIsVisibleForTesting("motor/Current"))
        << "motor/Current should remain visible";
    ASSERT_TRUE(window.TileIsVisibleForTesting("sensor/Temperature"))
        << "sensor/Temperature should remain visible";

    // Simulate auto-reconnect: disconnect + reconnect cycle.
    // This replays the streaming-mode setup from StartTransport().
    window.SimulateStreamingReconnectForTesting();

    // The hidden tile must still be hidden after reconnect.
    EXPECT_FALSE(window.TileIsVisibleForTesting("motor/RPM"))
        << "motor/RPM must stay hidden after reconnect";
    EXPECT_TRUE(window.TileIsVisibleForTesting("motor/Current"))
        << "motor/Current must remain visible after reconnect";
    EXPECT_TRUE(window.TileIsVisibleForTesting("sensor/Temperature"))
        << "sensor/Temperature must remain visible after reconnect";
}
