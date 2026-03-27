#pragma once

/// @file mjpeg_stream_source.h
/// @brief MJPEG-over-HTTP stream reader.
///
/// Connects to a standard MJPEG HTTP stream (multipart/x-mixed-replace),
/// parses boundary-delimited JPEG frames, decodes them via QImage, and
/// emits FrameReady signals.
///
/// Ian: This uses QNetworkAccessManager which is event-driven on the Qt
/// event loop — no dedicated thread needed.  JPEG decode via
/// QImage::loadFromData is fast enough for typical FRC camera resolutions
/// (320x240 to 640x480 at 15-30 fps) on the main thread.  If profiling
/// shows frame decode blocking the UI at higher resolutions, move the
/// decode step to a worker thread with a queued signal connection.

#include "camera_stream_source.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>

class QNetworkReply;

namespace sd::camera
{
    /// @brief MJPEG-over-HTTP stream source.
    ///
    /// Protocol: MJPEG streams use `multipart/x-mixed-replace` content type.
    /// Each JPEG frame is delimited by a boundary string from the HTTP headers.
    ///
    /// Ian: The parser handles two common server behaviors:
    /// 1. Servers that include Content-Length per part (fast path — read
    ///    exact byte count).
    /// 2. Servers that omit Content-Length (scan for next boundary marker).
    /// Both are common in FRC camera servers (cscore, mjpg-streamer).
    class MjpegStreamSource final : public CameraStreamSource
    {
        Q_OBJECT

    public:
        explicit MjpegStreamSource(QObject* parent = nullptr);
        ~MjpegStreamSource() override;

        void Start(const QString& url) override;
        void Stop() override;
        State GetState() const override;
        QString GetLastError() const override;

        /// @brief Return the measured frame rate (frames per second).
        double GetFps() const;

    private slots:
        void OnReadyRead();
        void OnStreamFinished();
        void OnStreamError();

    private:
        void ParseBuffer();
        bool ParseBoundaryFromContentType(const QString& contentType);
        void ProcessJpegFrame(const QByteArray& jpegData);
        void SetState(State newState);
        void SetError(const QString& message);

        QNetworkAccessManager m_networkManager;
        QNetworkReply* m_reply = nullptr;
        State m_state = State::Disconnected;
        QString m_lastError;

        // Ian: MJPEG multipart parsing state.
        // m_boundary is extracted from the Content-Type header on first response.
        // m_buffer accumulates incoming data between readyRead signals.
        // m_boundaryParsed gates the first-time boundary extraction.
        QByteArray m_boundary;
        QByteArray m_buffer;
        bool m_boundaryParsed = false;

        // Ian: FPS measurement.  We track the last N frame timestamps
        // and compute the rate from the window.  Simple and accurate
        // enough for a display counter without introducing a timer.
        QElapsedTimer m_fpsTimer;
        int m_frameCount = 0;
        double m_fps = 0.0;
        static constexpr int kFpsMeasureIntervalMs = 1000;
        qint64 m_lastFpsMeasureMs = 0;
    };
}
