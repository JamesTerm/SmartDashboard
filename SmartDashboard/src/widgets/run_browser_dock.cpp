#include "widgets/run_browser_dock.h"

#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QSet>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace sd::widgets
{
    // Ian: Custom data roles stashed on QStandardItem so we can tell runs from
    // folders from signal leaves without maintaining a parallel lookup map.
    // Values start at Qt::UserRole + 100 to avoid collisions with any future
    // Qt or project roles.
    namespace
    {
        constexpr int kRoleNodeKind  = Qt::UserRole + 100;  // "run", "group", "signal"
        constexpr int kRoleRunIndex  = Qt::UserRole + 101;  // int index into m_runs
        constexpr int kRoleSignalKey = Qt::UserRole + 102;  // full signal key string

        constexpr int kNodeKindRun    = 0;
        constexpr int kNodeKindGroup  = 1;
        constexpr int kNodeKindSignal = 2;

        // Ian: Find the sorted insertion row for a new child under parentItem.
        // Groups sort before leaves (like a file explorer), and within each
        // kind items are sorted case-insensitively by display name.  This
        // gives a stable, predictable tree order regardless of the arrival
        // order of keys from the transport or replay file.
        int FindSortedInsertionRow(QStandardItem* parentItem, int newNodeKind, const QString& newName)
        {
            const int count = parentItem->rowCount();
            const bool newIsGroup = (newNodeKind == kNodeKindGroup);

            for (int row = 0; row < count; ++row)
            {
                QStandardItem* child = parentItem->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }
                const int childKind = child->data(kRoleNodeKind).toInt();
                const bool childIsGroup = (childKind == kNodeKindGroup);

                // Groups come before leaves.
                if (newIsGroup && !childIsGroup)
                {
                    // Insert new group before the first non-group.
                    return row;
                }
                if (!newIsGroup && childIsGroup)
                {
                    // New leaf — skip past all groups.
                    continue;
                }

                // Same kind — compare names case-insensitively.
                if (newName.compare(child->text(), Qt::CaseInsensitive) < 0)
                {
                    return row;
                }
            }
            // Append at the end.
            return count;
        }
    }

    RunBrowserDock::RunBrowserDock(QWidget* parent)
        : QDockWidget("Run Browser", parent)
    {
        setObjectName("runBrowserDock");
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

        auto* container = new QWidget(this);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        // Ian: The Run Browser intentionally has no toolbar buttons.
        // The tree itself (checkboxes, right-click context menus) is the
        // only interaction surface.  A "Clear" button was removed because
        // it confused users — the dock's content is driven entirely by
        // the layout's tile lifecycle and transport connection state.

        m_treeModel = new QStandardItemModel(this);
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        // Ian: itemChanged fires when the user toggles a checkbox.  We use it
        // to propagate check state (group -> run tri-state rollup) and emit
        // the checked-signals-changed signal to the main window.
        connect(
            m_treeModel,
            &QStandardItemModel::itemChanged,
            this,
            &RunBrowserDock::OnModelItemChanged
        );

        m_treeView = new QTreeView(container);
        m_treeView->setModel(m_treeModel);
        m_treeView->setHeaderHidden(false);
        m_treeView->setRootIsDecorated(true);
        m_treeView->setAlternatingRowColors(true);
        m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_treeView->header()->setStretchLastSection(true);
        m_treeView->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_treeView->header()->setDefaultSectionSize(200);
        layout->addWidget(m_treeView, 1);

        connect(
            m_treeView,
            &QTreeView::activated,
            this,
            &RunBrowserDock::OnTreeActivated
        );

        m_summaryLabel = new QLabel("No runs loaded", container);
        m_summaryLabel->setStyleSheet("QLabel { color: #8c8c8c; }");
        layout->addWidget(m_summaryLabel);

        setWidget(container);
    }

    bool RunBrowserDock::AddRunFromFile(const QString& filePath)
    {
        LoadedRun run;
        if (!ParseCaptureSessionFile(filePath, run))
        {
            return false;
        }

        m_runs.push_back(std::move(run));
        RebuildTree();
        UpdateSummaryLabel();
        return true;
    }

    int RunBrowserDock::AddRunsFromFiles(const QStringList& filePaths)
    {
        int loaded = 0;
        for (const QString& path : filePaths)
        {
            LoadedRun run;
            if (ParseCaptureSessionFile(path, run))
            {
                m_runs.push_back(std::move(run));
                ++loaded;
            }
        }

        if (loaded > 0)
        {
            RebuildTree();
            UpdateSummaryLabel();
        }

        return loaded;
    }

    void RunBrowserDock::ClearAllRuns()
    {
        m_runs.clear();
        // Ian: ClearAllRuns is the ONLY method that exits streaming mode.
        // It is called when switching to a replay file (reading mode).
        // ClearDiscoveredKeys deliberately does NOT set m_streamingMode = false
        // because it needs to stay in streaming mode for TileAdded signals
        // that follow a layout-load.  ClearAllRuns is the hard reset.
        m_streamingMode = false;
        m_streamingRootItem = nullptr;  // Owned by model — cleared below.
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        m_streamingHiddenKeys.clear();

        RebuildTree();
        UpdateSummaryLabel();

        // Ian: Emit with empty sets so the consumer (MainWindow) knows all
        // signal keys are now unchecked and can show all tiles again.
        emit CheckedSignalsChanged(QSet<QString>(), QMap<QString, QString>());
    }

    int RunBrowserDock::RunCount() const
    {
        return static_cast<int>(m_runs.size());
    }

    QString RunBrowserDock::GetLoadedFilePath() const
    {
        if (m_runs.empty())
        {
            return {};
        }
        return m_runs[0].filePath;
    }

    // ====================================================================
    // Streaming mode
    // ====================================================================

    void RunBrowserDock::SetStreamingRootLabel(const QString& label)
    {
        m_streamingRootLabel = label;

        // Ian: Initialize or re-initialize streaming mode.  Every call clears
        // prior state and recreates the root node from scratch.  This is the
        // simplest way to guarantee the root label is always correct when
        // switching transports — ClearDiscoveredKeys is called first by
        // StartTransport, but even if the caller forgets, this method is
        // self-contained and idempotent.
        m_runs.clear();
        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});
        m_streamingMode = true;
        m_streamingRootItem = nullptr;  // Cleared by m_treeModel->clear().
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        EnsureStreamingRootNode();
    }

    // Ian: OnTileAdded is the layout-mirror entry point.  Each call from
    // MainWindow::TileAdded signal feeds one tile key into the tree.  The
    // two rules that make this simple:
    //   1. Build the tree as tiles appear on the layout — this method creates group/leaf nodes.
    //   2. Everything visible by default — groups start Qt::Checked.
    //
    // Streaming mode must be initialized first via SetStreamingRootLabel().
    // If not in streaming mode, this is a no-op (tiles created during replay
    // mode or before any transport starts are ignored).
    //
    // Subsequent calls are no-ops if the key already exists (e.g. reconnect
    // re-delivers the same key set).
    //
    // We suppress itemChanged during insertion to avoid per-node signal storms,
    // then emit CheckedSignalsChanged once at the end so MainWindow gets a
    // single consolidated update.

    void RunBrowserDock::OnTileAdded(const QString& key, const QString& type)
    {
        if (key.isEmpty())
        {
            return;
        }

        // Ian: Only act when streaming mode has been explicitly initialized
        // by SetStreamingRootLabel().  This prevents tiles created during
        // replay mode or before any transport starts from accidentally
        // switching the dock into streaming mode.
        //
        // Stress test: load a replay file, then File → Open Layout (or
        // drag-drop a layout).  The layout-load calls GetOrCreateTile for
        // every tile, which emits TileAdded.  Without this guard, each
        // TileAdded would create streaming tree nodes on top of the
        // file-driven replay tree — corrupting it.
        if (!m_streamingMode)
        {
            return;
        }

        // No-op for duplicate keys.
        if (m_discoveredKeys.contains(key))
        {
            return;
        }

        m_discoveredKeys.insert(key);
        m_discoveredKeyTypes.insert(key, type);

        // Split key on '/' and build the group/leaf hierarchy.
        const QStringList parts = key.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            return;
        }

        // Ian: All groups and leaves nest under the streaming root node,
        // which is the synthetic transport-named folder (e.g. "Direct").
        // If no root label was set, fall back to the invisible root so
        // the tree still works (just without the wrapper folder).
        m_suppressCheckSignal = true;

        QStandardItem* parent = (m_streamingRootItem != nullptr)
            ? m_streamingRootItem
            : m_treeModel->invisibleRootItem();
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            parent = GetOrCreateGroupItemForStreaming(parent, parts[i]);
        }

        // Leaf: the signal name.
        // Ian: Signal leaves are checkable so each tile can be individually
        // hidden/shown.  In streaming mode they start checked (visible by
        // default), matching the group behavior.  Hidden-keys application
        // below may flip individual leaves to unchecked.
        const QString leafName = parts.back();
        auto* signalItem = new QStandardItem(leafName);
        signalItem->setData(kNodeKindSignal, kRoleNodeKind);
        signalItem->setData(-1, kRoleRunIndex);  // No run index in streaming mode.
        signalItem->setData(key, kRoleSignalKey);
        signalItem->setCheckable(true);
        signalItem->setCheckState(Qt::Checked);

        auto* signalDetailItem = new QStandardItem(type);
        signalDetailItem->setData(kNodeKindSignal, kRoleNodeKind);
        signalDetailItem->setData(-1, kRoleRunIndex);
        signalDetailItem->setData(key, kRoleSignalKey);

        // Ian: Insert leaves in sorted position (groups before leaves, A-Z
        // within each kind) so the tree reads like a file explorer.
        const int leafRow = FindSortedInsertionRow(parent, kNodeKindSignal, leafName);
        parent->insertRow(leafRow, {signalItem, signalDetailItem});

        // Ian: Lazy hidden-keys application.  If SetHiddenDiscoveredKeys was
        // called before keys arrived (the normal reconnect path), we stored
        // the hidden set in m_streamingHiddenKeys.  Now that this leaf exists,
        // uncheck it if its key is hidden.  Then recompute ancestor group
        // tri-states up to the root.
        if (!m_streamingHiddenKeys.isEmpty())
        {
            if (m_streamingHiddenKeys.contains(key))
            {
                signalItem->setCheckState(Qt::Unchecked);
            }

            // Walk from the leaf's parent up, recomputing group tri-states.
            QStandardItem* ancestor = parent;
            while (ancestor != nullptr
                   && ancestor != m_treeModel->invisibleRootItem()
                   && ancestor != m_streamingRootItem)
            {
                const int kind = ancestor->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    UpdateGroupCheckState(ancestor);
                }
                ancestor = ancestor->parent();
            }

            // Update the streaming root's tri-state to reflect its children.
            if (m_streamingRootItem != nullptr)
            {
                UpdateRunCheckState(m_streamingRootItem);
            }
        }
        else
        {
            // Ian: Even when there are no hidden keys to apply, the root
            // tri-state still needs updating.  After a remove-then-re-add
            // cycle the root may have been set to Qt::Unchecked when its
            // last child was removed.  Re-adding a key creates a new checked
            // leaf, but without this call the root would stay unchecked
            // and CollectAndEmitCheckedSignals would skip the subtree.
            if (m_streamingRootItem != nullptr)
            {
                UpdateRunCheckState(m_streamingRootItem);
            }
        }

        m_suppressCheckSignal = false;

        UpdateSummaryLabel();
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::ClearDiscoveredKeys()
    {
        // Ian: Only clear streaming state when we are actually in streaming
        // mode.  This method is connected to MainWindow::TilesCleared, which
        // fires on "Clear Widgets" and layout-load.  In reading mode (replay
        // file loaded) the tree is driven entirely by the file — layout
        // operations must not touch it.  OnTileAdded and OnTileRemoved already
        // have this guard; ClearDiscoveredKeys needs it too.
        //
        // Stress test (reading mode): load a replay file, then press
        // "Clear Widgets" or File → Open Layout.  Without this guard the
        // TilesCleared signal would wipe the model, destroying the replay
        // tree the student just loaded.
        if (!m_streamingMode)
        {
            return;
        }

        // Ian: Stay in streaming mode so that TileAdded signals from the
        // subsequent layout-load can repopulate the tree.  If we set
        // m_streamingMode = false here, OnTileAdded would be a no-op for
        // every tile the layout creates, leaving the tree empty and all
        // tiles hidden.
        //
        // Stress test (streaming mode): connect to Direct, then
        // File → Open Layout (Replace).  OnLoadLayoutReplace calls
        // OnClearWidgets (fires TilesCleared → here) then LoadLayoutFromPath
        // (fires TileAdded per tile).  We must stay in streaming mode so
        // those TileAdded signals rebuild the tree.
        m_streamingRootItem = nullptr;  // Owned by model — cleared below.
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        m_streamingHiddenKeys.clear();

        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        // Recreate the root node so the tree is ready for incoming TileAdded.
        EnsureStreamingRootNode();

        UpdateSummaryLabel();

        emit CheckedSignalsChanged(QSet<QString>(), QMap<QString, QString>());
    }

    // Ian: OnTileRemoved is called when a tile is removed from the layout
    // (user right-click "Remove").  We remove the corresponding leaf node
    // from the streaming tree and prune any parent groups that become empty.
    // The tree stays a 1:1 mirror of what tiles exist on the layout.

    void RunBrowserDock::OnTileRemoved(const QString& key)
    {
        // Ian: Guard — in reading mode, tile removals on the layout must not
        // alter the file-driven replay tree.
        //
        // Stress test: load a replay file whose signals created tiles on the
        // layout, then right-click a tile and choose "Remove".  Without this
        // guard the removal would try to prune tree nodes that belong to the
        // replay, corrupting the tree structure.
        if (!m_streamingMode || key.isEmpty())
        {
            return;
        }

        if (!m_discoveredKeys.contains(key))
        {
            return;
        }

        m_discoveredKeys.remove(key);
        m_discoveredKeyTypes.remove(key);
        m_streamingHiddenKeys.remove(key);

        RemoveStreamingLeafAndPruneAncestors(key);

        UpdateSummaryLabel();
        CollectAndEmitCheckedSignals();
    }

    // Ian: Walk the streaming tree to find the signal leaf with the given key,
    // remove it, and then walk up removing any parent groups that became empty.
    // The streaming root node is never removed by this — only groups and leaves.

    void RunBrowserDock::RemoveStreamingLeafAndPruneAncestors(const QString& key)
    {
        QStandardItem* rootItem = m_streamingRootItem;
        if (rootItem == nullptr)
        {
            return;
        }

        // Recursive search: find the leaf node with kRoleSignalKey == key.
        std::function<QStandardItem*(QStandardItem*)> findLeaf;
        findLeaf = [&](QStandardItem* parent) -> QStandardItem*
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    if (child->data(kRoleSignalKey).toString() == key)
                    {
                        return child;
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    QStandardItem* found = findLeaf(child);
                    if (found != nullptr)
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        };

        QStandardItem* leaf = findLeaf(rootItem);
        if (leaf == nullptr)
        {
            return;
        }

        // Remove the leaf from its parent, then prune empty ancestors
        // up to (but not including) the streaming root.
        m_suppressCheckSignal = true;

        QStandardItem* parent = leaf->parent();
        if (parent == nullptr)
        {
            parent = m_treeModel->invisibleRootItem();
        }
        parent->removeRow(leaf->row());

        // Walk up: remove empty group parents.
        while (parent != nullptr
               && parent != rootItem
               && parent != m_treeModel->invisibleRootItem()
               && parent->rowCount() == 0)
        {
            QStandardItem* grandparent = parent->parent();
            if (grandparent == nullptr)
            {
                grandparent = m_treeModel->invisibleRootItem();
            }
            grandparent->removeRow(parent->row());
            parent = grandparent;
        }

        // Update the streaming root's tri-state.
        UpdateRunCheckState(rootItem);

        m_suppressCheckSignal = false;
    }

    int RunBrowserDock::DiscoveredKeyCount() const
    {
        return m_discoveredKeys.size();
    }

    bool RunBrowserDock::HasDiscoveredKey(const QString& key) const
    {
        return m_discoveredKeys.contains(key);
    }

    // Ian: GetHiddenDiscoveredKeys returns the inverse of the checked set —
    // all discovered keys whose parent group is NOT checked.  In streaming
    // mode we persist hidden keys rather than checked keys because the default
    // is "everything visible."  This means a reconnect starts with all keys
    // visible, then re-applies the hidden set as keys re-arrive.

    QSet<QString> RunBrowserDock::GetHiddenDiscoveredKeys() const
    {
        if (!m_streamingMode)
        {
            return {};
        }

        // Collect the currently checked (visible) keys.
        const QSet<QString> checkedKeys = GetCheckedSignalKeysForTesting();

        // Hidden = discovered - checked.
        QSet<QString> hidden;
        for (const QString& key : m_discoveredKeys)
        {
            if (!checkedKeys.contains(key))
            {
                hidden.insert(key);
            }
        }
        return hidden;
    }

    // Ian: SetHiddenDiscoveredKeys unchecks individual signal leaves whose
    // keys are in the hidden set, then recomputes ancestor group and root
    // tri-states.  Called after a reconnect when keys re-arrive and we want
    // to restore the user's previous opt-out choices.

    void RunBrowserDock::SetHiddenDiscoveredKeys(const QSet<QString>& hiddenKeys)
    {
        // Ian: Always store the hidden set so that OnTileAdded can lazily
        // apply it as new keys arrive.  Even if the tree is currently empty
        // (e.g. called right after ClearDiscoveredKeys before any keys arrive),
        // storing the set here means future OnTileAdded calls will create
        // leaves with the correct check state.
        m_streamingHiddenKeys = hiddenKeys;

        if (!m_streamingMode || hiddenKeys.isEmpty())
        {
            return;
        }

        // Recursive helper: uncheck leaves that are in the hidden set,
        // then recompute group tri-states bottom-up.
        std::function<void(QStandardItem*)> applyHidden;
        applyHidden = [&](QStandardItem* item)
        {
            for (int row = 0; row < item->rowCount(); ++row)
            {
                QStandardItem* child = item->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (hiddenKeys.contains(key))
                    {
                        child->setCheckState(Qt::Unchecked);
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    applyHidden(child);
                    UpdateGroupCheckState(child);
                }
            }
        };

        m_suppressCheckSignal = true;

        QStandardItem* rootItem = m_streamingRootItem;
        if (rootItem == nullptr)
        {
            m_suppressCheckSignal = false;
            return;
        }

        applyHidden(rootItem);

        // Update the streaming root's tri-state to reflect its children.
        UpdateRunCheckState(rootItem);

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::UncheckSignalByKey(const QString& key)
    {
        // Ian: Recursive helper to find a signal leaf by key under any parent.
        // Works in both reading and streaming modes — it searches by kRoleSignalKey
        // regardless of tree structure.
        std::function<QStandardItem*(QStandardItem*)> findLeaf;
        findLeaf = [&](QStandardItem* parent) -> QStandardItem*
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    if (child->data(kRoleSignalKey).toString() == key)
                    {
                        return child;
                    }
                }
                else if (kind == kNodeKindGroup || kind == kNodeKindRun)
                {
                    QStandardItem* found = findLeaf(child);
                    if (found != nullptr)
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        };

        QStandardItem* root = m_treeModel->invisibleRootItem();
        QStandardItem* leaf = findLeaf(root);
        if (leaf == nullptr || leaf->checkState() == Qt::Unchecked)
        {
            return;  // Key not in tree or already unchecked.
        }

        m_suppressCheckSignal = true;
        leaf->setCheckState(Qt::Unchecked);

        // Recompute ancestor tri-states up to the run/root node.
        QStandardItem* ancestor = leaf->parent();
        while (ancestor != nullptr)
        {
            const int ancestorKind = ancestor->data(kRoleNodeKind).toInt();
            if (ancestorKind == kNodeKindGroup)
            {
                UpdateGroupCheckState(ancestor);
            }
            else if (ancestorKind == kNodeKindRun)
            {
                UpdateRunCheckState(ancestor);
                break;
            }
            ancestor = ancestor->parent();
        }

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::UncheckSignalsByKeys(const QSet<QString>& keys)
    {
        if (keys.isEmpty())
        {
            return;
        }

        // Ian: Batch version of UncheckSignalByKey.  Single tree walk finds all
        // matching leaves, unchecks them, collects the set of affected ancestors,
        // then recomputes tri-states once and emits a single CheckedSignalsChanged.
        // This avoids N signal emissions and N tree walks when hiding N tiles.

        // Recursive helper — collects all matching leaves.
        QSet<QStandardItem*> affectedAncestors;
        std::function<void(QStandardItem*)> findAndUncheck;
        findAndUncheck = [&](QStandardItem* parent)
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    if (keys.contains(child->data(kRoleSignalKey).toString())
                        && child->checkState() != Qt::Unchecked)
                    {
                        child->setCheckState(Qt::Unchecked);

                        // Collect all ancestors for tri-state recompute.
                        QStandardItem* ancestor = child->parent();
                        while (ancestor != nullptr)
                        {
                            affectedAncestors.insert(ancestor);
                            ancestor = ancestor->parent();
                        }
                    }
                }
                else if (kind == kNodeKindGroup || kind == kNodeKindRun)
                {
                    findAndUncheck(child);
                }
            }
        };

        m_suppressCheckSignal = true;

        QStandardItem* root = m_treeModel->invisibleRootItem();
        findAndUncheck(root);

        if (affectedAncestors.isEmpty())
        {
            m_suppressCheckSignal = false;
            return;  // No leaves were unchecked.
        }

        // Recompute tri-states bottom-up.  Groups first (may be nested), then
        // run nodes.  UpdateGroupCheckState only reads children, so order among
        // sibling groups doesn't matter — we just need groups before their
        // ancestor runs.
        for (QStandardItem* item : affectedAncestors)
        {
            const int kind = item->data(kRoleNodeKind).toInt();
            if (kind == kNodeKindGroup)
            {
                UpdateGroupCheckState(item);
            }
        }
        for (QStandardItem* item : affectedAncestors)
        {
            const int kind = item->data(kRoleNodeKind).toInt();
            if (kind == kNodeKindRun)
            {
                UpdateRunCheckState(item);
            }
        }

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::EnsureStreamingRootNode()
    {
        if (m_streamingRootItem != nullptr)
        {
            return;
        }

        // Ian: The streaming root is structurally identical to a run node in
        // reading mode — a top-level checkable folder that contains groups.
        // Using kNodeKindRun lets OnModelItemChanged and UpdateRunCheckState
        // handle tri-state push-down/roll-up without any mode branching.
        // It starts Qt::Checked because streaming mode defaults to visible.
        const QString label = m_streamingRootLabel.isEmpty()
            ? QStringLiteral("Live")
            : m_streamingRootLabel;

        auto* rootItem = new QStandardItem(label);
        rootItem->setData(kNodeKindRun, kRoleNodeKind);
        rootItem->setData(-1, kRoleRunIndex);  // No run index in streaming mode.
        rootItem->setCheckable(true);
        rootItem->setCheckState(Qt::Checked);

        auto* rootDetailItem = new QStandardItem();
        rootDetailItem->setData(kNodeKindRun, kRoleNodeKind);
        rootDetailItem->setData(-1, kRoleRunIndex);
        m_treeModel->appendRow({rootItem, rootDetailItem});

        m_streamingRootItem = rootItem;

        // Expand the root so groups are immediately visible.
        if (m_treeView != nullptr)
        {
            m_treeView->expand(m_treeModel->indexFromItem(rootItem));
        }
    }

    bool RunBrowserDock::IsStreamingModeForTesting() const
    {
        return m_streamingMode;
    }

    // Ian: Streaming-mode group creation.  Like GetOrCreateGroupItem but
    // groups start Qt::Checked (visible by default).  This is the key
    // behavioral difference from reading mode where groups start unchecked.

    QStandardItem* RunBrowserDock::GetOrCreateGroupItemForStreaming(QStandardItem* parentItem, const QString& groupName)
    {
        // Search existing children for a matching group folder.
        for (int row = 0; row < parentItem->rowCount(); ++row)
        {
            QStandardItem* child = parentItem->child(row, 0);
            if (child != nullptr
                && child->data(kRoleNodeKind).toInt() == kNodeKindGroup
                && child->text() == groupName)
            {
                return child;
            }
        }

        // Create a new folder item — starts CHECKED in streaming mode.
        auto* groupItem = new QStandardItem(groupName);
        groupItem->setData(kNodeKindGroup, kRoleNodeKind);
        groupItem->setCheckable(true);
        groupItem->setCheckState(Qt::Checked);

        auto* emptyDetail = new QStandardItem();
        emptyDetail->setData(kNodeKindGroup, kRoleNodeKind);
        // Ian: Insert groups in sorted position (groups before leaves, A-Z).
        const int groupRow = FindSortedInsertionRow(parentItem, kNodeKindGroup, groupName);
        parentItem->insertRow(groupRow, {groupItem, emptyDetail});
        return groupItem;
    }

    bool RunBrowserDock::ParseCaptureSessionFileForTesting(const QString& filePath, LoadedRun& outRun)
    {
        return ParseCaptureSessionFile(filePath, outRun);
    }

    const LoadedRun& RunBrowserDock::GetRunForTesting(int index) const
    {
        return m_runs[static_cast<size_t>(index)];
    }

    QStandardItemModel* RunBrowserDock::GetTreeModelForTesting() const
    {
        return m_treeModel;
    }

    void RunBrowserDock::OnTreeActivated(const QModelIndex& index)
    {
        if (!index.isValid())
        {
            return;
        }

        // Resolve column-0 index for data lookup (user may click column 1).
        const QModelIndex col0 = index.siblingAtColumn(0);
        const QStandardItem* item = m_treeModel->itemFromIndex(col0);
        if (item == nullptr)
        {
            return;
        }

        const int nodeKind = item->data(kRoleNodeKind).toInt();
        const int runIndex = item->data(kRoleRunIndex).toInt();

        if (nodeKind == kNodeKindSignal)
        {
            const QString signalKey = item->data(kRoleSignalKey).toString();
            emit SignalActivated(runIndex, signalKey);
        }
        else if (nodeKind == kNodeKindRun)
        {
            emit RunActivated(runIndex);
        }
    }

    void RunBrowserDock::UpdateSummaryLabel()
    {
        if (m_summaryLabel == nullptr)
        {
            return;
        }

        if (m_streamingMode)
        {
            const int keyCount = m_discoveredKeys.size();
            if (keyCount == 0)
            {
                m_summaryLabel->setText("No signals discovered");
            }
            else
            {
                m_summaryLabel->setText(
                    QString("%1 signal%2 discovered")
                        .arg(keyCount)
                        .arg(keyCount == 1 ? "" : "s")
                );
            }
            return;
        }

        if (m_runs.empty())
        {
            m_summaryLabel->setText("No runs loaded");
            return;
        }

        int totalSignals = 0;
        for (const LoadedRun& run : m_runs)
        {
            totalSignals += static_cast<int>(run.signalInfos.size());
        }

        m_summaryLabel->setText(
            QString("%1 run%2, %3 signal%4")
                .arg(m_runs.size())
                .arg(m_runs.size() == 1 ? "" : "s")
                .arg(totalSignals)
                .arg(totalSignals == 1 ? "" : "s")
        );
    }

    void RunBrowserDock::RebuildTree()
    {
        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        for (int runIdx = 0; runIdx < static_cast<int>(m_runs.size()); ++runIdx)
        {
            const LoadedRun& run = m_runs[static_cast<size_t>(runIdx)];

            // Top-level: run node.
            QString runLabel = run.metadata.label;
            if (runLabel.isEmpty())
            {
                // Fall back to file name.
                const int lastSlash = std::max(run.filePath.lastIndexOf('/'), run.filePath.lastIndexOf('\\'));
                runLabel = (lastSlash >= 0) ? run.filePath.mid(lastSlash + 1) : run.filePath;
            }

            auto* runItem = new QStandardItem(runLabel);
            runItem->setData(kNodeKindRun, kRoleNodeKind);
            runItem->setData(runIdx, kRoleRunIndex);
            runItem->setToolTip(run.filePath);
            runItem->setCheckable(true);
            runItem->setCheckState(Qt::Unchecked);

            // Build details string from tags.
            QStringList tagParts;
            for (const auto& [tagKey, tagValue] : run.metadata.tags)
            {
                tagParts.append(QString("%1=%2").arg(tagKey, tagValue));
            }
            QString details = QString("%1 signals").arg(run.signalInfos.size());
            if (!tagParts.isEmpty())
            {
                details += QString("  [%1]").arg(tagParts.join(", "));
            }
            if (run.metadata.durationSec > 0.0)
            {
                details += QString("  %1s").arg(run.metadata.durationSec, 0, 'f', 3);
            }

            auto* runDetailItem = new QStandardItem(details);
            runDetailItem->setData(kNodeKindRun, kRoleNodeKind);
            runDetailItem->setData(runIdx, kRoleRunIndex);
            m_treeModel->appendRow({runItem, runDetailItem});

            // Ian: Build the grouped signal tree.  Signal keys use '/' as a
            // separator (e.g. "flush_fence/TotalMs").  We create folder nodes
            // for each prefix component, and leaf nodes for the final metric.
            // This handles arbitrary depth — if a key has more than one '/',
            // nested folders are created.
            for (const RunSignalInfo& sig : run.signalInfos)
            {
                const QStringList parts = sig.key.split('/', Qt::SkipEmptyParts);
                if (parts.isEmpty())
                {
                    continue;
                }

                // Walk/create the folder chain.
                QStandardItem* parent = runItem;
                for (int i = 0; i < parts.size() - 1; ++i)
                {
                    parent = GetOrCreateGroupItem(parent, parts[i]);
                }

                // Leaf: the metric name.
                // Ian: Signal leaves are checkable so each tile can be
                // individually hidden/shown.  In reading mode they start
                // unchecked — the user opts in by checking a group (which
                // pushes checked state to its leaves) or by checking
                // individual leaves.
                const QString leafName = parts.back();
                auto* signalItem = new QStandardItem(leafName);
                signalItem->setData(kNodeKindSignal, kRoleNodeKind);
                signalItem->setData(runIdx, kRoleRunIndex);
                signalItem->setData(sig.key, kRoleSignalKey);
                signalItem->setCheckable(true);
                signalItem->setCheckState(Qt::Unchecked);

                auto* signalDetailItem = new QStandardItem(
                    QString("%1  (%2 samples)").arg(sig.type).arg(sig.sampleCount)
                );
                signalDetailItem->setData(kNodeKindSignal, kRoleNodeKind);
                signalDetailItem->setData(runIdx, kRoleRunIndex);
                signalDetailItem->setData(sig.key, kRoleSignalKey);

                // Ian: Insert leaves in sorted position (groups before leaves, A-Z).
                const int leafRow = FindSortedInsertionRow(parent, kNodeKindSignal, leafName);
                parent->insertRow(leafRow, {signalItem, signalDetailItem});
            }
        }

        // Expand all run-level nodes by default.
        if (m_treeView != nullptr)
        {
            for (int i = 0; i < m_treeModel->rowCount(); ++i)
            {
                m_treeView->expand(m_treeModel->index(i, 0));
            }
        }
    }

    QStandardItem* RunBrowserDock::GetOrCreateGroupItem(QStandardItem* parentItem, const QString& groupName)
    {
        // Search existing children for a matching group folder.
        for (int row = 0; row < parentItem->rowCount(); ++row)
        {
            QStandardItem* child = parentItem->child(row, 0);
            if (child != nullptr
                && child->data(kRoleNodeKind).toInt() == kNodeKindGroup
                && child->text() == groupName)
            {
                return child;
            }
        }

        // Create a new folder item.
        auto* groupItem = new QStandardItem(groupName);
        groupItem->setData(kNodeKindGroup, kRoleNodeKind);
        groupItem->setCheckable(true);
        groupItem->setCheckState(Qt::Unchecked);

        auto* emptyDetail = new QStandardItem();
        emptyDetail->setData(kNodeKindGroup, kRoleNodeKind);
        // Ian: Insert groups in sorted position (groups before leaves, A-Z).
        const int groupRow = FindSortedInsertionRow(parentItem, kNodeKindGroup, groupName);
        parentItem->insertRow(groupRow, {groupItem, emptyDetail});
        return groupItem;
    }

    bool RunBrowserDock::ParseCaptureSessionFile(const QString& filePath, LoadedRun& outRun)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return false;
        }

        const QByteArray raw = file.readAll();
        if (raw.trimmed().isEmpty())
        {
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }

        const QJsonObject root = doc.object();

        // Require the "signals" array — this is the capture-session schema.
        if (!root.contains("signals") || !root.value("signals").isArray())
        {
            return false;
        }

        outRun.filePath = filePath;

        // Parse metadata (optional).
        if (root.contains("metadata") && root.value("metadata").isObject())
        {
            const QJsonObject meta = root.value("metadata").toObject();
            outRun.metadata.label = meta.value("label").toString();
            outRun.metadata.runId = meta.value("run_id").toString();
            outRun.metadata.startTimeUtc = meta.value("start_time_utc").toString();
            outRun.metadata.durationSec = meta.value("duration_sec").toDouble();
            outRun.metadata.capturedUpdateCount = meta.value("captured_update_count").toInt();

            if (meta.contains("tags") && meta.value("tags").isObject())
            {
                const QJsonObject tags = meta.value("tags").toObject();
                for (auto it = tags.constBegin(); it != tags.constEnd(); ++it)
                {
                    outRun.metadata.tags[it.key()] = it.value().toVariant().toString();
                }
            }
        }

        // Parse signal summaries (we don't load samples into memory here —
        // that's the replay transport's job).
        const QJsonArray signalArray = root.value("signals").toArray();
        outRun.signalInfos.reserve(static_cast<size_t>(signalArray.size()));

        for (const QJsonValue& signalValue : signalArray)
        {
            if (!signalValue.isObject())
            {
                continue;
            }

            const QJsonObject signalObj = signalValue.toObject();
            RunSignalInfo info;
            info.key = signalObj.value("key").toString();
            info.type = signalObj.value("type").toString().trimmed().toLower();
            info.sampleCount = signalObj.value("sample_count").toInt();

            if (info.sampleCount == 0 && signalObj.contains("samples"))
            {
                // Fallback: count the samples array.
                info.sampleCount = signalObj.value("samples").toArray().size();
            }

            if (!info.key.isEmpty())
            {
                outRun.signalInfos.push_back(std::move(info));
            }
        }

        return !outRun.signalInfos.empty();
    }

    // Ian: Checkbox propagation logic.
    //
    // Every node (run, group, signal leaf) is checkable.  The rules are:
    //
    //   1. Signal leaf toggled → recompute parent group's tri-state, then
    //      recompute the ancestor run/root tri-state.
    //   2. Group toggled → push the new state to all child leaves (and sub-groups
    //      recursively), then recompute the parent run/root tri-state.
    //   3. Run/root toggled → push the new state to all group children (which
    //      in turn push to their leaves).
    //
    // m_suppressCheckSignal prevents recursive re-entry: setting child check
    // states from code fires itemChanged again, so we guard against it.

    void RunBrowserDock::OnModelItemChanged(QStandardItem* item)
    {
        if (m_suppressCheckSignal || item == nullptr)
        {
            return;
        }

        // Only react to column-0 items (checkboxes live there).
        if (item->column() != 0)
        {
            return;
        }

        const int nodeKind = item->data(kRoleNodeKind).toInt();

        if (nodeKind == kNodeKindSignal)
        {
            // Ian: A signal leaf was toggled by the user.  Recompute the
            // parent group's tri-state (and the grandparent run's tri-state).
            m_suppressCheckSignal = true;

            QStandardItem* parentItem = item->parent();
            if (parentItem != nullptr)
            {
                const int parentKind = parentItem->data(kRoleNodeKind).toInt();
                if (parentKind == kNodeKindGroup)
                {
                    UpdateGroupCheckState(parentItem);

                    // Walk up to the run/root ancestor and update its tri-state.
                    QStandardItem* ancestor = parentItem->parent();
                    while (ancestor != nullptr)
                    {
                        const int ancestorKind = ancestor->data(kRoleNodeKind).toInt();
                        if (ancestorKind == kNodeKindRun)
                        {
                            UpdateRunCheckState(ancestor);
                            break;
                        }
                        else if (ancestorKind == kNodeKindGroup)
                        {
                            UpdateGroupCheckState(ancestor);
                        }
                        ancestor = ancestor->parent();
                    }
                }
                else if (parentKind == kNodeKindRun)
                {
                    // Single-segment key directly under the run/root.
                    UpdateRunCheckState(parentItem);
                }
            }

            m_suppressCheckSignal = false;
            CollectAndEmitCheckedSignals();
        }
        else if (nodeKind == kNodeKindGroup)
        {
            // Ian: A group was toggled — push the new state to all descendant
            // leaves and sub-groups, then recompute the ancestor run/root.
            const Qt::CheckState newState = item->checkState();

            m_suppressCheckSignal = true;
            PushCheckStateToDescendants(item, newState);

            // Walk up ancestors to update their tri-state.
            QStandardItem* ancestor = item->parent();
            while (ancestor != nullptr)
            {
                const int ancestorKind = ancestor->data(kRoleNodeKind).toInt();
                if (ancestorKind == kNodeKindRun)
                {
                    UpdateRunCheckState(ancestor);
                    break;
                }
                else if (ancestorKind == kNodeKindGroup)
                {
                    UpdateGroupCheckState(ancestor);
                }
                ancestor = ancestor->parent();
            }

            m_suppressCheckSignal = false;
            CollectAndEmitCheckedSignals();
        }
        else if (nodeKind == kNodeKindRun)
        {
            // A run was toggled — push the new binary state to all descendants.
            // (Tri-state partial is only computed from children, never set by user click.)
            const Qt::CheckState newState = item->checkState();
            const Qt::CheckState childState = (newState == Qt::PartiallyChecked)
                ? Qt::Checked   // Clicking partial toggles to fully checked.
                : newState;

            m_suppressCheckSignal = true;
            PushCheckStateToDescendants(item, childState);
            // Ian: Force the root to the user's intended state rather than
            // recomputing from children.  This respects the user's intent:
            // unchecked means "hide everything", checked means "show everything".
            item->setCheckState(childState);
            m_suppressCheckSignal = false;
            CollectAndEmitCheckedSignals();
        }
    }

    // Ian: Push a check state down to all descendants of a parent node
    // (groups, sub-groups, and signal leaves).  Used when a group or run
    // is toggled to cascade the state to everything beneath it.

    void RunBrowserDock::PushCheckStateToDescendants(QStandardItem* parent, Qt::CheckState state)
    {
        for (int row = 0; row < parent->rowCount(); ++row)
        {
            QStandardItem* child = parent->child(row, 0);
            if (child == nullptr)
            {
                continue;
            }

            const int kind = child->data(kRoleNodeKind).toInt();
            if (kind == kNodeKindGroup)
            {
                child->setCheckState(state);
                PushCheckStateToDescendants(child, state);
            }
            else if (kind == kNodeKindSignal)
            {
                child->setCheckState(state);
            }
        }
    }

    // Ian: Compute a group node's tri-state from its immediate children.
    // A group is Checked if all children are checked, Unchecked if none are,
    // and PartiallyChecked otherwise.  Children can be signal leaves or
    // sub-groups — both are checkable.

    void RunBrowserDock::UpdateGroupCheckState(QStandardItem* groupItem)
    {
        if (groupItem == nullptr)
        {
            return;
        }

        int checkedCount = 0;
        int partialCount = 0;
        int childCount = 0;

        for (int row = 0; row < groupItem->rowCount(); ++row)
        {
            const QStandardItem* child = groupItem->child(row, 0);
            if (child == nullptr || !child->isCheckable())
            {
                continue;
            }

            ++childCount;
            if (child->checkState() == Qt::Checked)
            {
                ++checkedCount;
            }
            else if (child->checkState() == Qt::PartiallyChecked)
            {
                ++partialCount;
            }
        }

        Qt::CheckState newState = Qt::Unchecked;
        if (childCount > 0)
        {
            if (checkedCount == childCount)
            {
                newState = Qt::Checked;
            }
            else if (checkedCount > 0 || partialCount > 0)
            {
                newState = Qt::PartiallyChecked;
            }
        }

        groupItem->setCheckState(newState);
    }

    void RunBrowserDock::UpdateRunCheckState(QStandardItem* runItem)
    {
        if (runItem == nullptr)
        {
            return;
        }

        int checkedCount = 0;
        int partialCount = 0;
        int childCount = 0;

        for (int row = 0; row < runItem->rowCount(); ++row)
        {
            const QStandardItem* child = runItem->child(row, 0);
            if (child == nullptr || !child->isCheckable())
            {
                continue;
            }

            ++childCount;
            if (child->checkState() == Qt::Checked)
            {
                ++checkedCount;
            }
            else if (child->checkState() == Qt::PartiallyChecked)
            {
                ++partialCount;
            }
        }

        Qt::CheckState newState = Qt::Unchecked;
        if (childCount > 0)
        {
            if (checkedCount == childCount)
            {
                newState = Qt::Checked;
            }
            else if (checkedCount > 0 || partialCount > 0)
            {
                newState = Qt::PartiallyChecked;
            }
        }

        runItem->setCheckState(newState);
    }

    void RunBrowserDock::CollectAndEmitCheckedSignals()
    {
        QSet<QString> checkedKeys;
        QMap<QString, QString> keyToType;

        // Ian: Recursive leaf collector — walks a parent node and adds all
        // signal-leaf descendants that are individually checked.  Each leaf
        // owns its own check state, so we only include leaves with
        // Qt::Checked.  In streaming mode the type comes from
        // m_discoveredKeyTypes; in reading mode from m_runs.
        std::function<void(const QStandardItem*, int)> collectLeaves;
        collectLeaves = [&](const QStandardItem* parent, int runIdx)
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                const QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    if (child->checkState() != Qt::Checked)
                    {
                        continue;
                    }
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        checkedKeys.insert(key);
                        if (m_streamingMode)
                        {
                            const auto typeIt = m_discoveredKeyTypes.find(key);
                            if (typeIt != m_discoveredKeyTypes.end())
                            {
                                keyToType.insert(key, typeIt.value());
                            }
                        }
                        else if (runIdx >= 0 && runIdx < static_cast<int>(m_runs.size()))
                        {
                            for (const RunSignalInfo& sig : m_runs[static_cast<size_t>(runIdx)].signalInfos)
                            {
                                if (sig.key == key)
                                {
                                    keyToType.insert(key, sig.type);
                                    break;
                                }
                            }
                        }
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    // Recurse into groups regardless of check state —
                    // a partially-checked group can have individually
                    // checked leaves inside.
                    collectLeaves(child, runIdx);
                }
            }
        };

        if (m_streamingMode)
        {
            // Ian: Streaming mode — walk the synthetic root node.
            // If the root is unchecked, nothing is visible.
            QStandardItem* rootItem = m_streamingRootItem;
            if (rootItem == nullptr)
            {
                emit CheckedSignalsChanged(checkedKeys, keyToType);
                return;
            }

            if (rootItem->checkState() == Qt::Unchecked)
            {
                emit CheckedSignalsChanged(checkedKeys, keyToType);
                return;
            }

            collectLeaves(rootItem, -1);
        }
        else
        {
            // Reading mode — walk run nodes.
            for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
            {
                const QStandardItem* runItem = m_treeModel->item(runRow, 0);
                if (runItem == nullptr)
                {
                    continue;
                }

                // If the run is completely unchecked, skip its subtree.
                if (runItem->checkState() == Qt::Unchecked)
                {
                    continue;
                }

                const int runIdx = runItem->data(kRoleRunIndex).toInt();
                if (runIdx < 0 || runIdx >= static_cast<int>(m_runs.size()))
                {
                    continue;
                }

                collectLeaves(runItem, runIdx);
            }
        }

        emit CheckedSignalsChanged(checkedKeys, keyToType);
    }

    // Ian: Programmatically check signal leaves whose keys are in the given
    // set, then recompute group and run tri-states.  Used by MainWindow to
    // restore persisted checked state on startup.  We walk every leaf in the
    // tree and check it if its key is in signalKeys, then update ancestor
    // group/run tri-states to match.

    void RunBrowserDock::SetCheckedGroupsBySignalKeys(const QSet<QString>& signalKeys)
    {
        if (signalKeys.isEmpty())
        {
            return;
        }

        // Recursive helper: check matching leaves and return true if any were checked.
        std::function<bool(QStandardItem*)> checkLeaves;
        checkLeaves = [&](QStandardItem* parentItem) -> bool
        {
            bool hasMatch = false;
            for (int row = 0; row < parentItem->rowCount(); ++row)
            {
                QStandardItem* child = parentItem->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (signalKeys.contains(key))
                    {
                        child->setCheckState(Qt::Checked);
                        hasMatch = true;
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    if (checkLeaves(child))
                    {
                        hasMatch = true;
                    }
                    // Recompute this group's tri-state from its children.
                    UpdateGroupCheckState(child);
                }
            }

            return hasMatch;
        };

        m_suppressCheckSignal = true;

        for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
        {
            QStandardItem* runItem = m_treeModel->item(runRow, 0);
            if (runItem == nullptr)
            {
                continue;
            }

            checkLeaves(runItem);

            // Update the run node tri-state after all children are set.
            UpdateRunCheckState(runItem);
        }

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    // Ian: Build a display-text path from the root to the given item, using '/'
    // as separator.  This produces e.g. "MyRun/flush_fence" for a group node.
    // Used to persist/restore expanded state across sessions.

    QString RunBrowserDock::BuildItemPath(const QStandardItem* item)
    {
        QStringList parts;
        const QStandardItem* current = item;
        while (current != nullptr)
        {
            parts.prepend(current->text());
            current = current->parent();
        }
        return parts.join('/');
    }

    void RunBrowserDock::CollectExpandedPaths(const QModelIndex& parent, QStringList& outPaths) const
    {
        if (m_treeView == nullptr)
        {
            return;
        }

        const int rowCount = m_treeModel->rowCount(parent);
        for (int row = 0; row < rowCount; ++row)
        {
            const QModelIndex index = m_treeModel->index(row, 0, parent);
            if (m_treeView->isExpanded(index))
            {
                const QStandardItem* item = m_treeModel->itemFromIndex(index);
                if (item != nullptr)
                {
                    outPaths.append(BuildItemPath(item));
                }
                // Recurse into expanded children.
                CollectExpandedPaths(index, outPaths);
            }
        }
    }

    QStringList RunBrowserDock::GetExpandedPaths() const
    {
        QStringList paths;
        CollectExpandedPaths(QModelIndex(), paths);
        return paths;
    }

    void RunBrowserDock::SetExpandedPaths(const QStringList& paths)
    {
        if (m_treeView == nullptr || paths.isEmpty())
        {
            return;
        }

        const QSet<QString> pathSet(paths.begin(), paths.end());

        // Recursive helper: walk the tree and expand matching nodes.
        std::function<void(const QModelIndex&)> restore;
        restore = [&](const QModelIndex& parent)
        {
            const int rowCount = m_treeModel->rowCount(parent);
            for (int row = 0; row < rowCount; ++row)
            {
                const QModelIndex index = m_treeModel->index(row, 0, parent);
                const QStandardItem* item = m_treeModel->itemFromIndex(index);
                if (item != nullptr && pathSet.contains(BuildItemPath(item)))
                {
                    m_treeView->expand(index);
                    restore(index);
                }
            }
        };

        restore(QModelIndex());
    }

    QSet<QString> RunBrowserDock::GetCheckedSignalKeysForTesting() const
    {
        QSet<QString> result;

        // Ian: Recursive collector — walks the tree and adds all signal
        // leaves that are individually checked (Qt::Checked).
        std::function<void(const QStandardItem*)> collect;
        collect = [&](const QStandardItem* parent)
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                const QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    // Recurse into groups — a partially-checked group
                    // can still have individually checked leaves.
                    collect(child);
                }
                else if (kind == kNodeKindSignal && child->checkState() == Qt::Checked)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        result.insert(key);
                    }
                }
            }
        };

        if (m_streamingMode)
        {
            if (m_streamingRootItem == nullptr)
            {
                return result;
            }

            // If root is unchecked, nothing is visible.
            if (m_streamingRootItem->checkState() == Qt::Unchecked)
            {
                return result;
            }

            collect(m_streamingRootItem);
        }
        else
        {
            // Reading mode — walk run nodes.
            for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
            {
                const QStandardItem* runItem = m_treeModel->item(runRow, 0);
                if (runItem == nullptr || runItem->checkState() == Qt::Unchecked)
                {
                    continue;
                }

                collect(runItem);
            }
        }

        return result;
    }
}
