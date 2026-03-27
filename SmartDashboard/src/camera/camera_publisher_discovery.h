#pragma once

/// @file camera_publisher_discovery.h
/// @brief Monitors NT4 /CameraPublisher/ keys to auto-discover cameras.
///
/// Ian: CameraPublisherDiscovery is deliberately decoupled from the NT4
/// transport plugin.  It consumes the same key-update callbacks that tiles
/// consume — MainWindow routes updates to it.  This avoids linking the
/// camera feature to a specific transport.  Any transport that delivers
/// /CameraPublisher/ keys will work.

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace sd::camera
{
    /// @brief Discovers cameras from NT4 /CameraPublisher/ key updates.
    ///
    /// Key schema (from WPILib CameraServer):
    ///   /CameraPublisher/{CameraName}/streams      -> string[] (URLs with prefix)
    ///   /CameraPublisher/{CameraName}/source        -> string
    ///   /CameraPublisher/{CameraName}/description   -> string
    ///   /CameraPublisher/{CameraName}/connected     -> boolean
    ///
    /// Stream URL format: "mjpg:http://{address}:{port}/?action=stream"
    /// Strip the "mjpg:" prefix to get the raw HTTP URL.
    class CameraPublisherDiscovery final : public QObject
    {
        Q_OBJECT

    public:
        explicit CameraPublisherDiscovery(QObject* parent = nullptr);

        /// @brief Process an NT4 key-value update.
        ///
        /// Ian: MainWindow calls this for every variable update.  We check if
        /// the key starts with "/CameraPublisher/" and extract camera info.
        /// This is called on the UI thread (same as tile updates), so no
        /// threading concerns.
        void OnVariableUpdate(const QString& key, int valueType, const QVariant& value);

        /// @brief Clear all discovered cameras.
        ///
        /// Ian: Called on transport disconnect/switch, following the same
        /// pattern as RunBrowserDock::ClearDiscoveredKeys().
        void Clear();

        /// @brief Return discovered camera names.
        QStringList GetCameraNames() const;

        /// @brief Return stream URLs for a given camera name.
        QStringList GetStreamUrls(const QString& cameraName) const;

    signals:
        /// @brief Emitted when a camera is discovered or its streams change.
        /// @param name Camera name.
        /// @param urls Stream URLs (with mjpg: prefix stripped).
        void CameraDiscovered(const QString& name, const QStringList& urls);

        /// @brief Emitted when all cameras are cleared (e.g. on disconnect).
        void CamerasCleared();

    private:
        static QStringList ParseStreamUrls(const QVariant& value);
        static QString StripStreamPrefix(const QString& url);

        // Ian: Map of camera name -> list of clean (prefix-stripped) URLs.
        QMap<QString, QStringList> m_cameras;
    };
}
