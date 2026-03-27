#pragma once

/// @file camera_viewer_dock.h
/// @brief Dockable panel for viewing MJPEG camera streams.
///
/// Contains a CameraDisplayWidget for video rendering, and a toolbar with
/// camera selector combo, manual URL input, connect/disconnect buttons,
/// and a reticle toggle.
///
/// Ian: Follows the same dock pattern as RunBrowserDock:
///   - Object name for Qt state save/restore
///   - View menu toggle action synced via visibilityChanged
///   - Starts hidden, AllDockWidgetAreas allowed
///   - Created in MainWindow::SetupUi after the Run Browser dock
///
/// Camera stream lifecycle ties to transport lifecycle:
///   - StopTransport() -> StopStream() (stop camera stream)
///   - Disconnect -> ClearDiscoveredCameras() (clear camera selector)
///   This follows the same pattern as RunBrowserDock's ClearDiscoveredKeys.

#include <QDockWidget>
#include <QMap>
#include <QString>
#include <QStringList>

#include "camera/camera_stream_source.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace sd::camera
{
    class MjpegStreamSource;
}

namespace sd::widgets
{
    class CameraDisplayWidget;

    /// @brief Dockable panel for viewing camera streams.
    class CameraViewerDock final : public QDockWidget
    {
        Q_OBJECT

    public:
        explicit CameraViewerDock(QWidget* parent = nullptr);
        ~CameraViewerDock() override;

        /// @brief Stop the current camera stream (if any).
        ///
        /// Ian: Called by MainWindow when StopTransport() runs.  This ensures
        /// the camera stream is torn down at the same lifecycle point as the
        /// data transport.
        void StopStream();

        /// @brief Clear all discovered cameras from the selector combo.
        ///
        /// Ian: Called by MainWindow on disconnect, following the same pattern
        /// as RunBrowserDock::ClearDiscoveredKeys().
        void ClearDiscoveredCameras();

        /// @brief Add a discovered camera to the selector combo.
        /// @param name Human-readable camera name (e.g. "USB Camera").
        /// @param urls Available stream URLs for this camera.
        void AddDiscoveredCamera(const QString& name, const QStringList& urls);

        /// @brief Return the number of discovered cameras.
        int DiscoveredCameraCount() const;

    private slots:
        void OnConnectClicked();
        void OnDisconnectClicked();
        void OnCameraSelected(int index);
        void OnStreamStateChanged(sd::camera::CameraStreamSource::State newState);
        void OnFrameReceived(const QImage& frame);

    private:
        void SetupUi();
        void UpdateButtonStates();
        void ConnectToUrl(const QString& url);

        // Toolbar widgets.
        QComboBox* m_cameraCombo = nullptr;
        QLineEdit* m_urlEdit = nullptr;
        QPushButton* m_connectButton = nullptr;
        QPushButton* m_disconnectButton = nullptr;
        QCheckBox* m_reticleCheckBox = nullptr;
        QLabel* m_statusLabel = nullptr;

        // Display widget.
        CameraDisplayWidget* m_displayWidget = nullptr;

        // Stream source.
        sd::camera::MjpegStreamSource* m_streamSource = nullptr;

        // Discovered cameras: name -> list of URLs.
        QMap<QString, QStringList> m_discoveredCameras;
    };
}
