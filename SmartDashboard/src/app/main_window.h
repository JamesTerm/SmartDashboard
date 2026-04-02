#pragma once

#include "layout/layout_serializer.h"
#include "model/variable_store.h"
#include "transport/dashboard_transport.h"
#include "widgets/variable_tile.h"

#include <QMainWindow>
#include <QByteArray>
#include <QMap>
#include <QSet>
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
class QRubberBand;
class QTimer;
class QGroupBox;
#ifdef _DEBUG
class QLocalServer;
class QLocalSocket;
#endif

namespace sd::camera
{
    class CameraDiscoveryAggregator;
    class CameraPublisherDiscovery;
    class StaticCameraSource;
}

namespace sd::widgets
{
    class CameraViewerDock;
    class PlaybackTimelineWidget;
    class RunBrowserDock;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool startTransportOnInit = true);
    ~MainWindow() override;

signals:
    // Ian: Layout-tile lifecycle signals.  The Run Browser dock (streaming mode)
    // subscribes to these to mirror the layout — it builds its tree from what
    // tiles actually exist, not from raw transport keys.  This decouples the
    // Run Browser from transport details and means the tree reflects tiles
    // loaded from saved layouts, created by transports, or added by any future
    // mechanism.
    void TileAdded(const QString& key, const QString& type);
    void TileRemoved(const QString& key);
    void TilesCleared();

public:
#ifdef SMARTDASHBOARD_TESTS
    void SetTransportSelectionForTesting(const QString& transportId, sd::transport::TransportKind kind);
    void SimulateVariableUpdateForTesting(const QString& key, int valueType, const QVariant& value, quint64 seq = 0);
    void SimulateControlDoubleEditForTesting(const QString& key, double value);
    void LoadRememberedControlValuesForTesting();
    bool LoadLayoutFromPathForTesting(const QString& path, bool applyToExistingTiles = true, bool persistAsCurrentPath = false);
    bool SaveLayoutToPathForTesting(const QString& path);
    void ClearWidgetsForTesting();
    int RememberedControlValueCountForTesting() const;
    bool HasRememberedControlValueForTesting(const QString& key) const;
    bool TileHasValueForTesting(const QString& key) const;
    bool TileIsTemporaryDefaultForTesting(const QString& key) const;
    bool TileIsVisibleForTesting(const QString& key) const;
    void SetConnectionFieldValueForTesting(const QString& fieldId, const QVariant& value);
    void SyncConnectionConfigToPluginSettingsJsonForTesting();
    bool GetConnectionFieldBoolForTesting(const QString& fieldId, bool defaultValue) const;
    void SimulateStreamingReconnectForTesting();
    sd::widgets::RunBrowserDock* GetRunBrowserDockForTesting() const;
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
    void OnHideTileRequested(const QString& key);
    void OnRunBrowserCheckedSignalsChanged(const QSet<QString>& checkedKeys, const QMap<QString, QString>& keyToType);
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
    bool IsAutoConnectEnabled() const;
    void OnReconnectTimerFired();
    void UpdatePlaybackUiState();
    void RefreshReplayMarkers();
    void RefreshReplayMarkerList(std::int64_t cursorUs);
    void RefreshReplaySummaryLabel();
    void LoadUserReplayBookmarks();
    void PersistUserReplayBookmarks() const;
    void RestoreDefaultReplayWorkspaceLayout();
    void UpdateReplayDockHeightLock();
    void PopulateRunBrowserFromReplayFile();
    void PersistRunBrowserState() const;
    void LoadRunBrowserState();
    void ResetAllLinePlots();
    void SeekPlaybackToUs(std::int64_t targetUs, bool rewindToStart = false);
    void StepPlaybackByUs(std::int64_t deltaUs);
    void StartSessionRecording();
    void StopSessionRecording();
    void RecordVariableEvent(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void RecordConnectionEvent(int state);
    bool IsRecordingTransportKind(sd::transport::TransportKind kind) const;
    void PublishRememberedControlValues();
    void RepublishPluginControlEdits();
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
    void ClearTileSelection();
    void SelectTilesInRect(const QRect& selectionRect);
    void HideSelectedTiles();
    void BeginGroupDrag(sd::widgets::VariableTile* anchorTile, const QPoint& globalPos);
    void UpdateGroupDrag(const QPoint& globalPos);
    void EndGroupDrag();

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
    QAction* m_runBrowserViewAction = nullptr;
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
    sd::widgets::RunBrowserDock* m_runBrowserDock = nullptr;
    QSet<QString> m_runBrowserCheckedKeys;  ///< Signal keys currently checked in Run Browser (controls tile visibility, persisted to QSettings).
    QStringList m_runBrowserExpandedPaths;  ///< Persisted expanded tree paths for Run Browser.
    // Ian: m_runBrowserActive distinguishes "no Run Browser session" (show all
    // tiles) from "Run Browser has a replay loaded but nothing checked" (hide
    // all replay tiles).  Without this flag, unchecking the last group would
    // make checkedKeys empty, which the old logic treated as "no filtering."
    bool m_runBrowserActive = false;  ///< True when the Run Browser dock has a replay file loaded.
    // Ian: m_runBrowserHiddenKeys stores streaming-mode opt-outs.  In streaming
    // mode we persist hidden keys (the inverse of checked keys) because the
    // default is "everything visible."  On reconnect, as keys re-arrive via
    // OnTileAdded, we re-apply this hidden set.  Not used in reading mode.
    QSet<QString> m_runBrowserHiddenKeys;  ///< Streaming-mode: keys the user has hidden (persisted).
    // Ian: Camera viewer dock — dockable MJPEG stream viewer.
    // CameraPublisherDiscovery monitors /CameraPublisher/ keys from any
    // transport to auto-populate the camera selector.  The dock + discovery
    // lifecycle follows the same pattern as RunBrowserDock: StopTransport()
    // stops the stream, disconnect clears discovered cameras.
    // Ian: Camera discovery — abstracted from transport.
    // The aggregator merges cameras from all discovery providers
    // (protocol-discovered, static/manual URLs, etc.) and feeds the
    // unified list to the dock.  Protocol-discovered cameras have no
    // display prefix; static cameras show "[Static]".
    // m_cameraDiscovery handles /CameraPublisher/ keys from any transport.
    // m_staticCameraSource handles user-configured persistent URLs.
    // m_cameraAggregator merges both and connects to m_cameraDock.
    QAction* m_cameraViewAction = nullptr;
    sd::widgets::CameraViewerDock* m_cameraDock = nullptr;
    sd::camera::CameraPublisherDiscovery* m_cameraDiscovery = nullptr;
    sd::camera::StaticCameraSource* m_staticCameraSource = nullptr;
    sd::camera::CameraDiscoveryAggregator* m_cameraAggregator = nullptr;
    QListWidget* m_replayMarkerList = nullptr;
    QLabel* m_replaySelectionSummaryLabel = nullptr;
    sd::widgets::PlaybackTimelineWidget* m_playbackTimeline = nullptr;
    QTimer* m_playbackUiTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    bool m_userDisconnected = false;
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
    // Ian: In-memory control edits for plugin transports (Native Link, NT4).
    // Unlike Direct's registry-persisted m_rememberedControlValues, these survive
    // only as long as the process runs.  On reconnect the server's snapshot seeds
    // controls with defaults (e.g. /selected -> "Do Nothing", doubles -> 0.0),
    // so we re-publish the user's last local edits to restore the operator's
    // intent.  Keyed by topic key, value carries the type tag + QVariant.
    // Covers choosers, doubles (e.g. TestMove), bools, and plain strings.
    struct PluginControlEdit
    {
        int valueType = 0;
        QVariant value;
    };
    std::unordered_map<std::string, PluginControlEdit> m_pluginControlEdits;
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

    // Ian: Multi-select lasso + group drag state.  The rubber band is drawn on
    // the canvas during a lasso drag (mouse press on empty canvas space then
    // drag).  On release, tiles whose geometry intersects the rubber band rect
    // join the selection.  When a selected tile is dragged, all selected tiles
    // move as a group.  Selection is cleared on Escape, click on empty space,
    // or when editable mode is turned off.
    QSet<sd::widgets::VariableTile*> m_selectedTiles;
    QRubberBand* m_lassoRubberBand = nullptr;
    QPoint m_lassoOrigin;
    bool m_lassoActive = false;
    bool m_groupDragActive = false;
    sd::widgets::VariableTile* m_groupDragAnchor = nullptr;
    bool m_groupDragUpdating = false;  // Ian: Re-entry guard — prevents sibling Move events from cascading.
    struct GroupDragEntry
    {
        sd::widgets::VariableTile* tile = nullptr;
        QPoint startPos;
    };
    std::vector<GroupDragEntry> m_groupDragEntries;
    bool m_syncingReplayTimelineDockVisibility = false;
    bool m_syncingReplayMarkerDockVisibility = false;
    bool m_syncingMarkerSelection = false;

#ifdef _DEBUG
    QLocalServer* m_debugCommandServer = nullptr;
#endif
};
