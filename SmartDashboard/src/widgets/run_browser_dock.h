#pragma once

/// @file run_browser_dock.h
/// @brief Dockable panel for browsing signal key hierarchies.
///
/// The Run Browser operates in two modes, selected by the caller:
///
/// **Reading mode** (Replay): The tree is populated up front from a
/// capture-session JSON file.  Groups start unchecked — the user opts in
/// to see tiles.  Top-level nodes are named runs from file metadata.
///
/// **Layout-mirror mode** (Live/Streaming): The tree mirrors whatever tiles
/// exist on the layout.  MainWindow emits TileAdded / TileRemoved /
/// TilesCleared signals; this dock subscribes and builds the tree
/// incrementally.  Groups start checked — everything is visible by default,
/// and the user opts out by unchecking.  A synthetic root node (named after
/// the transport, e.g. "Direct") holds all group folders and flat signal
/// leaves.  The root is checkable and supports tri-state.
///
/// Ian: The dock widget itself does not track which transport is active.
/// MainWindow drives the difference by calling different APIs:
///   - Reading:   ClearAllRuns() -> AddRunFromFile() -> user opts in
///   - Streaming:  ClearDiscoveredKeys() -> SetStreamingRootLabel()
///                 -> OnTileAdded() per tile -> user opts out
/// This keeps the dock a pure view/model with no transport awareness.

#include <QDockWidget>
#include <QMap>
#include <QModelIndex>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>
#include <map>

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QTreeView;
class QStandardItem;
class QStandardItemModel;

namespace sd::widgets
{
    /// @brief Metadata extracted from a capture-session JSON file.
    struct RunMetadata
    {
        QString label;
        QString runId;
        QString startTimeUtc;
        double durationSec = 0.0;
        int capturedUpdateCount = 0;
        std::map<QString, QString> tags;
    };

    /// @brief Summary of a single signal within a loaded run.
    struct RunSignalInfo
    {
        QString key;        ///< Full slash-delimited key, e.g. "flush_fence/TotalMs"
        QString type;       ///< "double", "bool", "string", etc.
        int sampleCount = 0;
    };

    /// @brief One loaded replay run (one file).
    struct LoadedRun
    {
        QString filePath;
        RunMetadata metadata;
        // Ian: Named 'signalInfos' to avoid colliding with Qt's 'signals' macro,
        // which MOC interprets as a keyword in any Q_OBJECT-adjacent header.
        std::vector<RunSignalInfo> signalInfos;
    };

    /// @brief Dockable tree panel for browsing multiple loaded replay runs.
    ///
    /// Ian: The dock is intended to sit on the left side of the main window.
    /// It does not own any transport — it just reads JSON files on demand and
    /// presents the hierarchy.  Future work will let the user select signals
    /// and push them to comparison plots.
    class RunBrowserDock final : public QDockWidget
    {
        Q_OBJECT

    public:
        explicit RunBrowserDock(QWidget* parent = nullptr);

        /// @brief Load a capture-session JSON file and add it to the tree.
        /// @param filePath Path to the JSON file.
        /// @return True if the file was parsed and added successfully.
        bool AddRunFromFile(const QString& filePath);

        /// @brief Load multiple files at once.
        /// @param filePaths Paths to JSON files.
        /// @return Number of files successfully loaded.
        int AddRunsFromFiles(const QStringList& filePaths);

        /// @brief Remove all loaded runs and clear the tree.
        void ClearAllRuns();

        /// @brief Return the number of currently loaded runs.
        int RunCount() const;

        /// @brief Return the file path of the loaded run at index 0, or empty if none.
        QString GetLoadedFilePath() const;

        /// @brief Programmatically check signal leaves whose keys match the given key set.
        ///
        /// Ian: Used to restore persisted checked state on startup.  Walks the tree
        /// and checks any signal leaf whose key is in @p signalKeys, then recomputes
        /// ancestor group and run tri-states.
        /// Emits CheckedSignalsChanged once at the end.
        void SetCheckedGroupsBySignalKeys(const QSet<QString>& signalKeys);

        /// @brief Return the tree paths (slash-joined display names) of all currently expanded nodes.
        QStringList GetExpandedPaths() const;

        /// @brief Expand tree nodes matching the given paths (as returned by GetExpandedPaths).
        void SetExpandedPaths(const QStringList& paths);

        // ----------------------------------------------------------------
        // Layout-mirror mode API (live/streaming connections)
        // ----------------------------------------------------------------

        /// @brief Set the display label for the streaming mode root node.
        ///
        /// Ian: The root node is the top-level checkable folder that contains
        /// all discovered groups and flat signal leaves.  It is named after the
        /// transport (e.g. "Direct", "Native Link", "NT4 (Glass)").  This must
        /// be called after ClearDiscoveredKeys() and before the first
        /// OnTileAdded().  The root node is created eagerly so the tree
        /// is never truly empty once streaming mode is initialized.
        void SetStreamingRootLabel(const QString& label);

        /// @brief Notify the dock that a tile was added to the layout.
        ///
        /// Creates `/`-delimited group nodes as needed.  Group nodes start
        /// checked so that all tiles are visible by default.
        /// No-op if the key already exists in the tree.
        /// Emits CheckedSignalsChanged so MainWindow stays in sync.
        ///
        /// Ian: This is the layout-mirror counterpart of AddRunFromFile().
        /// Connected to MainWindow::TileAdded.  The dock builds its tree
        /// as a 1:1 mirror of whatever tiles exist on the layout — it does
        /// not listen to the transport directly.  Because groups start
        /// checked, the Run Browser is an optional "opt-out" filter.
        void OnTileAdded(const QString& key, const QString& type);

        /// @brief Notify the dock that a tile was removed from the layout.
        ///
        /// Removes the corresponding leaf node from the tree and prunes
        /// any empty parent groups.  No-op if the key is not in the tree.
        /// Emits CheckedSignalsChanged so MainWindow stays in sync.
        ///
        /// Ian: Connected to MainWindow::TileRemoved.  When the user
        /// removes a tile via its context menu, this removes the tree entry
        /// so the tree stays a faithful mirror of the layout.
        void OnTileRemoved(const QString& key);

        /// @brief Remove all streaming-mode discovered keys and clear the tree.
        ///
        /// Ian: Called when switching transports or disconnecting.  Unlike
        /// ClearAllRuns() this only clears the streaming-mode state, leaving
        /// the m_runs vector untouched (which is empty anyway in streaming mode).
        void ClearDiscoveredKeys();

        /// @brief Return the number of discovered keys (streaming mode).
        int DiscoveredKeyCount() const;

        /// @brief Return true if the given key has already been discovered.
        bool HasDiscoveredKey(const QString& key) const;

        /// @brief Return the set of keys that are currently hidden (unchecked)
        /// in streaming mode.  Used for persistence — streaming mode persists
        /// hidden keys rather than checked keys.
        QSet<QString> GetHiddenDiscoveredKeys() const;

        /// @brief Apply a set of hidden keys to streaming-mode tree nodes.
        /// Groups containing only hidden descendant signals are unchecked.
        ///
        /// Ian: Called on reconnect to re-apply the user's previous opt-out
        /// choices as keys re-arrive.  The hidden set is persisted across
        /// sessions by MainWindow.  Also stores the set internally so that
        /// OnTileAdded can apply it to newly arriving keys — if a new
        /// key's parent group has ALL descendants hidden, the group starts
        /// unchecked.
        void SetHiddenDiscoveredKeys(const QSet<QString>& hiddenKeys);

        /// @brief Programmatically uncheck a single signal leaf by key.
        ///
        /// Finds the leaf node matching @p key in the tree (both reading
        /// and streaming modes), unchecks it, recomputes ancestor tri-states,
        /// and emits CheckedSignalsChanged.  No-op if the key is not in the tree.
        ///
        /// Ian: Called by MainWindow when the user right-clicks a tile and
        /// selects "Hide."  This routes through the standard checkbox pipeline
        /// so the Run Browser tree, hidden-keys persistence, and tile visibility
        /// all stay in sync.
        void UncheckSignalByKey(const QString& key);

        /// @brief Batch-uncheck multiple signal leaves by key.
        ///
        /// Same as calling UncheckSignalByKey for each key, but performs a
        /// single tree walk to find all matching leaves, unchecks them,
        /// recomputes ancestor tri-states once, and emits CheckedSignalsChanged
        /// exactly once at the end.
        ///
        /// Ian: Called by MainWindow when the user hides multiple selected
        /// tiles at once.  Using the single-key version in a loop would emit
        /// N signals and do N full tree walks — this avoids that.
        void UncheckSignalsByKeys(const QSet<QString>& keys);

        // ----------------------------------------------------------------
        // Testing API
        // ----------------------------------------------------------------

        /// @brief Parse a capture-session JSON file without adding to the dock.
        /// Exposed for testing.
        static bool ParseCaptureSessionFileForTesting(const QString& filePath, LoadedRun& outRun);

        /// @brief Return the loaded run at the given index.  Exposed for testing.
        const LoadedRun& GetRunForTesting(int index) const;

        /// @brief Return the tree model.  Exposed for testing.
        QStandardItemModel* GetTreeModelForTesting() const;

        /// @brief Return the set of currently checked signal keys.  Exposed for testing.
        QSet<QString> GetCheckedSignalKeysForTesting() const;

        /// @brief Return true if the dock is currently in streaming mode.
        bool IsStreamingModeForTesting() const;

    signals:
        /// @brief Emitted when a signal leaf node is activated (double-clicked).
        /// @param runIndex Index of the run in the loaded runs list.
        /// @param signalKey Full signal key string.
        void SignalActivated(int runIndex, const QString& signalKey);

        /// @brief Emitted when a run node is activated (double-clicked).
        /// @param runIndex Index of the run in the loaded runs list.
        void RunActivated(int runIndex);

        /// @brief Emitted when the set of checked (visible) signal keys changes.
        ///
        /// Ian: Each entry is a pair of {signalKey, signalType} for all signals
        /// under currently-checked groups across all runs.  The consumer
        /// (MainWindow) uses this to show/hide tiles.  The type string
        /// is the lowercase JSON type from the capture file ("double", "bool",
        /// "string", "int", etc.).
        void CheckedSignalsChanged(const QSet<QString>& checkedKeys,
                                   const QMap<QString, QString>& keyToType);

    private:
        void RebuildTree();
        QStandardItem* GetOrCreateGroupItem(QStandardItem* parentItem, const QString& groupName);
        static bool ParseCaptureSessionFile(const QString& filePath, LoadedRun& outRun);
        void OnTreeActivated(const QModelIndex& index);
        void UpdateSummaryLabel();
        void OnModelItemChanged(QStandardItem* item);
        void UpdateRunCheckState(QStandardItem* runItem);
        void UpdateGroupCheckState(QStandardItem* groupItem);
        void PushCheckStateToDescendants(QStandardItem* parent, Qt::CheckState state);
        void CollectAndEmitCheckedSignals();
        static QString BuildItemPath(const QStandardItem* item);
        void CollectExpandedPaths(const QModelIndex& parent, QStringList& outPaths) const;

        // Ian: Streaming-mode helpers.  GetOrCreateGroupItemForStreaming is
        // similar to the reading-mode GetOrCreateGroupItem but creates groups
        // with Qt::Checked initial state (visible by default).  We keep both
        // to avoid adding mode-awareness to GetOrCreateGroupItem, which would
        // risk breaking reading mode's unchecked-by-default behavior.
        // EnsureStreamingRootNode creates the synthetic top-level folder on
        // first use if a root label has been set.
        // RemoveStreamingLeafAndPruneAncestors removes a leaf node and prunes
        // any parent groups that become empty afterwards.
        QStandardItem* GetOrCreateGroupItemForStreaming(QStandardItem* parentItem, const QString& groupName);
        void EnsureStreamingRootNode();
        void RemoveStreamingLeafAndPruneAncestors(const QString& key);

        QTreeView* m_treeView = nullptr;
        QStandardItemModel* m_treeModel = nullptr;
        QLabel* m_summaryLabel = nullptr;
        std::vector<LoadedRun> m_runs;
        bool m_suppressCheckSignal = false;  ///< Guard against recursive itemChanged during bulk check updates.

        // Ian: Streaming-mode state.  m_streamingMode is true when the tree
        // is being built incrementally via OnTileAdded(), false when
        // populated from JSON files.  m_discoveredKeys tracks which keys have
        // already been added to avoid duplicates and to support persistence.
        // m_discoveredKeyTypes maps key -> type for the CheckedSignalsChanged
        // signal's keyToType parameter.
        // m_streamingRootItem is the synthetic top-level node named after the
        // transport (e.g. "Direct").  It uses kNodeKindRun so OnModelItemChanged
        // and UpdateRunCheckState can reuse the reading-mode tri-state logic.
        bool m_streamingMode = false;
        QStandardItem* m_streamingRootItem = nullptr;
        QString m_streamingRootLabel;
        QSet<QString> m_discoveredKeys;
        QMap<QString, QString> m_discoveredKeyTypes;
        // Ian: Stored hidden keys for streaming mode.  Set by SetHiddenDiscoveredKeys
        // and consulted by OnTileAdded to decide if a newly created group
        // should be unchecked (all its descendants are hidden).
        QSet<QString> m_streamingHiddenKeys;
    };
}
