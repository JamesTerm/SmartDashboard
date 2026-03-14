#pragma once

#include "layout/layout_serializer.h"
#include "model/variable_store.h"
#include "transport/dashboard_transport.h"
#include "widgets/variable_tile.h"

#include <QMainWindow>
#include <QByteArray>
#include <QVariant>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <cstdint>
#include <memory>
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

namespace sd::widgets
{
    class PlaybackTimelineWidget;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

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
    void OnUseNetworkTablesTransport();
    void OnUseReplayTransport();
    void OnToggleTelemetryFeature();
    void OnSetNtHost();
    void OnSetNtTeam();
    void OnToggleNtUseTeam();
    void OnOpenReplayFile();
    void OnRecordToggled(bool checked);
    void OnPlaybackRewindToStart();
    void OnPlaybackPlayPause();
    void OnPlaybackRateChanged(int index);
    void OnPlaybackCursorScrubbed(std::int64_t cursorUs);
    void OnPlaybackPreviousMarker();
    void OnPlaybackNextMarker();
    void OnReplayMarkerActivated(QListWidgetItem* item);
    void OnAddReplayBookmark();
    void OnClearReplayBookmarks();

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
    QString BuildDisplayLabel(const QString& key) const;
    void StartTransport();
    void StopTransport();
    void UpdatePlaybackUiState();
    void RefreshReplayMarkers();
    void RefreshReplayMarkerList(std::int64_t cursorUs);
    void RefreshReplaySummaryLabel();
    void LoadUserReplayBookmarks();
    void PersistUserReplayBookmarks() const;
    void StepPlaybackByUs(std::int64_t deltaUs);
    void StartSessionRecording();
    void StopSessionRecording();
    void RecordVariableEvent(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void RecordConnectionEvent(int state);
    bool IsRecordingTransportKind(sd::transport::TransportKind kind) const;

    QWidget* m_canvas = nullptr;
    QLabel* m_statusLabel = nullptr;
    QAction* m_editableAction = nullptr;
    QAction* m_snapToGridAction = nullptr;
    QAction* m_moveModeAction = nullptr;
    QAction* m_resizeModeAction = nullptr;
    QAction* m_moveResizeModeAction = nullptr;
    QAction* m_connectTransportAction = nullptr;
    QAction* m_disconnectTransportAction = nullptr;
    QAction* m_useDirectTransportAction = nullptr;
    QAction* m_useNetworkTablesTransportAction = nullptr;
    QAction* m_useReplayTransportAction = nullptr;
    QAction* m_telemetryFeatureViewAction = nullptr;
    QAction* m_replayControlsViewAction = nullptr;
    QAction* m_replayTimelineViewAction = nullptr;
    QAction* m_replayMarkersViewAction = nullptr;
    QAction* m_ntUseTeamAction = nullptr;
    QAction* m_openReplayFileAction = nullptr;
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
    std::unique_ptr<sd::transport::IDashboardTransport> m_transport;

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
    bool m_syncingReplayControlsDockVisibility = false;
    bool m_syncingReplayTimelineDockVisibility = false;
    bool m_syncingReplayMarkerDockVisibility = false;
    bool m_syncingMarkerSelection = false;
};
