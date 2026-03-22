#pragma once

#include "layout/layout_serializer.h"
#include "model/variable_store.h"
#include "transport/dashboard_transport.h"
#include "widgets/variable_tile.h"

#include <QMainWindow>
#include <QByteArray>
#include <QVariant>
#include <QVector>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <cstdint>
#include <memory>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class QAction;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QWidget;
class QMenu;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
class QTimer;
class QGroupBox;
#ifdef _DEBUG
class QLocalServer;
class QLocalSocket;
#endif

namespace sd::widgets
{
    class PlaybackTimelineWidget;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool startTransportOnInit = true);
    ~MainWindow() override;

#ifdef SMARTDASHBOARD_TESTS
    void SetTransportSelectionForTesting(const QString& transportId, sd::transport::TransportKind kind);
    void SimulateVariableUpdateForTesting(const QString& key, int valueType, const QVariant& value, quint64 seq = 0);
    void SimulateControlDoubleEditForTesting(const QString& key, double value);
    void LoadRememberedControlValuesForTesting();
    bool LoadLayoutFromPathForTesting(const QString& path, bool applyToExistingTiles = true, bool persistAsCurrentPath = false);
    void ClearWidgetsForTesting();
    int RememberedControlValueCountForTesting() const;
    bool HasRememberedControlValueForTesting(const QString& key) const;
    bool TileHasValueForTesting(const QString& key) const;
    bool TileIsTemporaryDefaultForTesting(const QString& key) const;
    void SetConnectionFieldValueForTesting(const QString& fieldId, const QVariant& value);
    void SyncConnectionConfigToPluginSettingsJsonForTesting();
    bool GetConnectionFieldBoolForTesting(const QString& fieldId, bool defaultValue) const;
#endif

private slots:
    void OnToggleEditable();
    void OnToggleSnapToGrid();
    void OnSetMoveMode();
    void OnSetResizeMode();
    void OnSetMoveResizeMode();
    void OnVariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void OnConnectionStateChanged(int state);
    void OnSaveLayout();
    void OnSaveLayoutAs();
    void OnLoadLayout();
    void OnLoadLayoutReplace();
    void OnImportLegacyXmlLayout();
    void OnClearWidgets();
    void OnRemoveWidgetRequested(const QString& key);
    void OnControlBoolEdited(const QString& key, bool value);
    void OnControlDoubleEdited(const QString& key, double value);
    void OnControlStringEdited(const QString& key, const QString& value);
    void OnConnectTransport();
    void OnDisconnectTransport();
    void OnUseDirectTransport();
    void OnUseReplayTransport();
    void OnToggleTelemetryFeature();
    void OnEditTransportSettings();
    void OnOpenReplayFile();
    void OnRecordToggled(bool checked);
    void OnResetAllLinePlots();
    void OnPlaybackRewindToStart();
    void OnPlaybackPlayPause();
    void OnPlaybackRateChanged(int index);
    void OnPlaybackCursorScrubbed(std::int64_t cursorUs);
    void OnPlaybackPreviousMarker();
    void OnPlaybackNextMarker();
    void OnReplayMarkerActivated(QListWidgetItem* item);
    void OnAddReplayBookmark();
    void OnClearReplayBookmarks();

#ifdef _DEBUG
    void OnDebugCommandReceived();
#endif

#ifdef SMARTDASHBOARD_TESTS
public:
#endif

private:
    using TileMap = std::unordered_map<std::string, sd::widgets::VariableTile*>;
    using LayoutMap = std::unordered_map<std::string, sd::layout::WidgetLayoutEntry>;

    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    sd::widgets::VariableTile* GetOrCreateTile(const QString& key, sd::widgets::VariableType type);
    void UpdateWindowConnectionText(int state);
    void LoadWindowGeometry();
    void SaveWindowGeometry() const;
    bool SaveLayoutToPath(const QString& path);
    bool SaveLayoutUsingCurrentOrPrompt();
    bool LoadLayoutFromPath(const QString& path, bool applyToExistingTiles, bool persistAsCurrentPath = true);
    QString GetInitialLayoutPath() const;
    void PersistLastLayoutPath(const QString& path) const;
    bool HasActiveJsonLayoutPath() const;
    QString GetLayoutTitleSegment() const;
    void RefreshWindowTitle();
    void MarkLayoutDirty();
    void ApplyTransportMenuChecks();
    void PersistConnectionSettings() const;
    void LoadRememberedControlValues();
    void SaveRememberedControlValues() const;
    void ApplyRememberedControlValuesToTiles();
    void RememberControlValueIfAllowed(const QString& key, int valueType, const QVariant& value, bool persistToSettings);
    QString BuildDisplayLabel(const QString& key) const;
    void StartTransport();
    void StopTransport();
    void UpdatePlaybackUiState();
    void RefreshReplayMarkers();
    void RefreshReplayMarkerList(std::int64_t cursorUs);
    void RefreshReplaySummaryLabel();
    void LoadUserReplayBookmarks();
    void PersistUserReplayBookmarks() const;
    void RestoreDefaultReplayWorkspaceLayout();
    void UpdateReplayDockHeightLock();
    void ResetAllLinePlots();
    void SeekPlaybackToUs(std::int64_t targetUs, bool rewindToStart = false);
    void StepPlaybackByUs(std::int64_t deltaUs);
    void StartSessionRecording();
    void StopSessionRecording();
    void RecordVariableEvent(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void RecordConnectionEvent(int state);
    bool IsRecordingTransportKind(sd::transport::TransportKind kind) const;
    void PublishRememberedControlValues();
    void DebugLogUiEvent(const QString& line) const;
    void DrainPendingUiUpdates();
    void SelectTransport(const QString& transportId);
    const sd::transport::TransportDescriptor* GetSelectedTransportDescriptor() const;
    QString GetSelectedTransportDisplayName() const;
    bool CurrentTransportUsesShortDisplayKeys() const;
    bool CurrentTransportUsesLegacyNtSettings() const;
    bool CurrentTransportSupportsChooser() const;
    bool CurrentTransportUsesRememberedControlValues() const;
    QVariant GetConnectionFieldValue(const sd::transport::ConnectionFieldDescriptor& field) const;
    void SetConnectionFieldValue(const QString& fieldId, const QVariant& value);
    void SyncConnectionConfigToPluginSettingsJson();
    void SyncConnectionConfigFromPluginSettingsJson();
    QString GetNativeLinkCarrierSetting() const;
    void SetNativeLinkCarrierSetting(const QString& carrier);
    bool ShouldShowNativeLinkCarrierDebugOptions() const;
    void ApplyTemporaryDefaultValuesToTiles();

    QWidget* m_canvas = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_playbackCursorStatusLabel = nullptr;
    QLabel* m_playbackWindowStatusLabel = nullptr;
    QAction* m_editableAction = nullptr;
    QAction* m_snapToGridAction = nullptr;
    QAction* m_moveModeAction = nullptr;
    QAction* m_resizeModeAction = nullptr;
    QAction* m_moveResizeModeAction = nullptr;
    QAction* m_connectTransportAction = nullptr;
    QAction* m_disconnectTransportAction = nullptr;
    QAction* m_useDirectTransportAction = nullptr;
    QAction* m_useReplayTransportAction = nullptr;
    QAction* m_editTransportSettingsAction = nullptr;
    QAction* m_telemetryFeatureViewAction = nullptr;
    QAction* m_replayControlsViewAction = nullptr;
    QAction* m_replayTimelineViewAction = nullptr;
    QAction* m_replayMarkersViewAction = nullptr;
    QAction* m_openReplayFileAction = nullptr;
    QAction* m_resetAllLinePlotsAction = nullptr;
    QAction* m_clearLinePlotsOnRewindAction = nullptr;
    QAction* m_clearLinePlotsOnBackwardSeekAction = nullptr;
    QWidget* m_telemetryControlsPanel = nullptr;
    QPushButton* m_recordButton = nullptr;
    QToolButton* m_rewindButton = nullptr;
    QToolButton* m_playPauseButton = nullptr;
    QToolButton* m_prevMarkerButton = nullptr;
    QToolButton* m_nextMarkerButton = nullptr;
    QToolButton* m_addBookmarkButton = nullptr;
    QToolButton* m_clearBookmarksButton = nullptr;
    QComboBox* m_playbackRateCombo = nullptr;
    QDockWidget* m_replayControlsDock = nullptr;
    QDockWidget* m_replayTimelineDock = nullptr;
    QDockWidget* m_replayMarkerDock = nullptr;
    QListWidget* m_replayMarkerList = nullptr;
    QLabel* m_replaySelectionSummaryLabel = nullptr;
    sd::widgets::PlaybackTimelineWidget* m_playbackTimeline = nullptr;
    QTimer* m_playbackUiTimer = nullptr;
    bool m_telemetryFeatureEnabled = true;
    bool m_recordRequested = false;
    bool m_isEditable = false;
    bool m_snapToGrid = true;
    sd::widgets::EditInteractionMode m_editInteractionMode = sd::widgets::EditInteractionMode::MoveAndResize;
    int m_nextTileOffset = 0;
    std::uint64_t m_lastTransportSeq = 0;
    int m_connectionState = static_cast<int>(sd::transport::ConnectionState::Disconnected);
    bool m_layoutDirty = false;
    bool m_suppressLayoutDirty = false;
    QString m_layoutFilePath;
    TileMap m_tiles;
    LayoutMap m_savedLayoutByKey;
    sd::model::VariableStore m_variableStore;
    sd::transport::ConnectionConfig m_connectionConfig;
    sd::transport::DashboardTransportRegistry m_transportRegistry;
    std::unique_ptr<sd::transport::IDashboardTransport> m_transport;
    std::vector<QAction*> m_pluginTransportActions;
    struct RememberedControlValue
    {
        int valueType = 3;
        QVariant value;
    };
    struct TemporaryDefaultValue
    {
        int valueType = 3;
        QVariant value;
    };
    std::unordered_map<std::string, RememberedControlValue> m_rememberedControlValues;
    std::unordered_map<std::string, TemporaryDefaultValue> m_temporaryDefaultValues;
    mutable std::ofstream m_uiDebugLog;
    std::mutex m_pendingUiUpdatesMutex;
    QVector<sd::transport::VariableUpdate> m_pendingUiUpdates;
    bool m_uiDrainScheduled = false;

    std::mutex m_recordingMutex;
    std::condition_variable m_recordingCv;
    std::deque<QByteArray> m_recordingQueue;
    std::thread m_recordingThread;
    bool m_recordingThreadRunning = false;
    bool m_recordingStopRequested = false;
    QString m_recordingFilePath;
    std::uint64_t m_recordingStartEpochUs = 0;
    std::uint64_t m_recordingLastTimestampUs = 0;
    std::uint64_t m_recordingStartSteadyUs = 0;
    std::vector<sd::transport::PlaybackMarker> m_replayMarkers;
    std::vector<sd::transport::PlaybackMarker> m_userReplayBookmarks;
    std::vector<std::int64_t> m_replayMarkerTimesUs;
    bool m_replayControlsPreferredVisible = true;
    bool m_replayTimelinePreferredVisible = true;
    bool m_replayMarkersPreferredVisible = true;
    bool m_clearLinePlotsOnRewind = false;
    bool m_clearLinePlotsOnBackwardSeek = false;
    bool m_syncingReplayControlsDockVisibility = false;
    bool m_syncingReplayTimelineDockVisibility = false;
    bool m_syncingReplayMarkerDockVisibility = false;
    bool m_syncingMarkerSelection = false;

#ifdef _DEBUG
    QLocalServer* m_debugCommandServer = nullptr;
#endif
};
