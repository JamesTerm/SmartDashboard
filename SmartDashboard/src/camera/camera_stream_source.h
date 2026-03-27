#pragma once

/// @file camera_stream_source.h
/// @brief Abstract interface for camera frame sources.
///
/// The display widget accepts decoded QImages from any backend that
/// implements this interface.  MJPEG HTTP is the first concrete source;
/// a Robot_Simulation MJPEG server or a test-pattern generator can be
/// added later without touching the display code.
///
/// Ian: This interface deliberately has no dependency on transport or
/// network details.  The display widget connects to FrameReady and
/// doesn't care where frames come from.  Keep it that way.

#include <QImage>
#include <QObject>
#include <QString>

namespace sd::camera
{
    /// @brief Abstract base for camera frame sources.
    ///
    /// Subclasses must emit FrameReady when a decoded frame is available,
    /// and StateChanged when the connection state changes.
    class CameraStreamSource : public QObject
    {
        Q_OBJECT

    public:
        enum class State
        {
            Disconnected,
            Connecting,
            Streaming,
            Error
        };

        explicit CameraStreamSource(QObject* parent = nullptr)
            : QObject(parent)
        {
        }

        ~CameraStreamSource() override = default;

        /// @brief Start streaming from the given URL.
        /// Emits StateChanged(Connecting) immediately, then either
        /// StateChanged(Streaming) + FrameReady signals on success,
        /// or StateChanged(Error) on failure.
        virtual void Start(const QString& url) = 0;

        /// @brief Stop the current stream and release resources.
        /// Emits StateChanged(Disconnected).
        virtual void Stop() = 0;

        /// @brief Return the current connection state.
        virtual State GetState() const = 0;

        /// @brief Return a human-readable error message, or empty if none.
        virtual QString GetLastError() const = 0;

    signals:
        /// @brief Emitted when a new decoded frame is available.
        void FrameReady(const QImage& frame);

        /// @brief Emitted when the connection state changes.
        void StateChanged(sd::camera::CameraStreamSource::State newState);
    };
}
