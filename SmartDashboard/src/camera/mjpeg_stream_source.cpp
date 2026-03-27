/// @file mjpeg_stream_source.cpp
/// @brief MJPEG-over-HTTP stream reader implementation.

#include "camera/mjpeg_stream_source.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace sd::camera
{

MjpegStreamSource::MjpegStreamSource(QObject* parent)
    : CameraStreamSource(parent)
{
}

MjpegStreamSource::~MjpegStreamSource()
{
    Stop();
}

void MjpegStreamSource::Start(const QString& url)
{
    Stop();

    m_lastError.clear();
    m_buffer.clear();
    m_boundary.clear();
    m_boundaryParsed = false;
    m_frameCount = 0;
    m_fps = 0.0;
    m_lastFpsMeasureMs = 0;

    SetState(State::Connecting);

    // Ian: Brace-init avoids the Most Vexing Parse — without braces the
    // compiler interprets `QNetworkRequest request(QUrl(url))` as a function
    // declaration rather than a variable definition.
    QNetworkRequest request{QUrl(url)};
    // Ian: Some MJPEG servers require an Accept header to serve the
    // multipart stream instead of a single JPEG snapshot.
    request.setRawHeader("Accept", "multipart/x-mixed-replace");

    m_reply = m_networkManager.get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, &MjpegStreamSource::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &MjpegStreamSource::OnStreamFinished);
    // Ian: errorOccurred was added in Qt 5.15 / Qt6.  We use it instead of
    // the deprecated error(QNetworkReply::NetworkError) signal.
    connect(m_reply, &QNetworkReply::errorOccurred, this, &MjpegStreamSource::OnStreamError);
}

void MjpegStreamSource::Stop()
{
    if (m_reply != nullptr)
    {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    m_buffer.clear();
    m_boundary.clear();
    m_boundaryParsed = false;
    m_frameCount = 0;
    m_fps = 0.0;

    if (m_state != State::Disconnected)
    {
        SetState(State::Disconnected);
    }
}

MjpegStreamSource::State MjpegStreamSource::GetState() const
{
    return m_state;
}

QString MjpegStreamSource::GetLastError() const
{
    return m_lastError;
}

double MjpegStreamSource::GetFps() const
{
    return m_fps;
}

void MjpegStreamSource::OnReadyRead()
{
    if (m_reply == nullptr)
    {
        return;
    }

    // Ian: On first data arrival, parse the boundary from the Content-Type
    // header.  We defer this until readyRead rather than doing it in the
    // metaDataChanged signal because some servers don't send headers until
    // the first data chunk.
    if (!m_boundaryParsed)
    {
        const QString contentType = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (!contentType.isEmpty())
        {
            if (!ParseBoundaryFromContentType(contentType))
            {
                SetError(QStringLiteral("Not an MJPEG stream (Content-Type: %1)").arg(contentType));
                Stop();
                return;
            }
            m_boundaryParsed = true;

            if (!m_fpsTimer.isValid())
            {
                m_fpsTimer.start();
            }
        }
    }

    m_buffer.append(m_reply->readAll());
    ParseBuffer();
}

void MjpegStreamSource::OnStreamFinished()
{
    // Ian: A finished signal on an MJPEG stream means the server closed the
    // connection.  This is always unexpected for a live stream — MJPEG
    // streams are meant to run indefinitely until the client disconnects.
    if (m_reply != nullptr && m_reply->error() == QNetworkReply::NoError)
    {
        SetError(QStringLiteral("Stream ended (server closed connection)"));
    }
    else if (m_reply != nullptr)
    {
        SetError(m_reply->errorString());
    }
    Stop();
}

void MjpegStreamSource::OnStreamError()
{
    if (m_reply != nullptr)
    {
        // Ian: OperationCanceledError fires when we call abort() in Stop().
        // Don't treat our own abort as an error.
        if (m_reply->error() == QNetworkReply::OperationCanceledError)
        {
            return;
        }
        SetError(m_reply->errorString());
    }
    Stop();
}

void MjpegStreamSource::ParseBuffer()
{
    if (m_boundary.isEmpty())
    {
        return;
    }

    // Ian: MJPEG multipart format:
    //   --<boundary>\r\n
    //   Content-Type: image/jpeg\r\n
    //   [Content-Length: <n>\r\n]
    //   \r\n
    //   <JPEG data>
    //   \r\n--<boundary>\r\n
    //   ...
    //
    // We search for the boundary marker to find frame boundaries.
    // The boundary in the Content-Type header may or may not include
    // the leading "--".  We normalize to always search for "\r\n--<boundary>"
    // (or just "--<boundary>" at the start of the buffer).

    const QByteArray marker = QByteArray("--") + m_boundary;
    const QByteArray headerEnd = QByteArray("\r\n\r\n");

    while (true)
    {
        // Find the start of a part (boundary marker).
        int markerPos = m_buffer.indexOf(marker);
        if (markerPos < 0)
        {
            break;
        }

        // Skip past the boundary marker and the CRLF after it.
        int headerStart = markerPos + marker.size();
        // Ian: Some servers put \r\n after the boundary, some put \n.
        // Skip any leading \r\n.
        if (headerStart < m_buffer.size() && m_buffer.at(headerStart) == '\r')
        {
            ++headerStart;
        }
        if (headerStart < m_buffer.size() && m_buffer.at(headerStart) == '\n')
        {
            ++headerStart;
        }

        // Find the end of the part headers (blank line).
        const int headerEndPos = m_buffer.indexOf(headerEnd, headerStart);
        if (headerEndPos < 0)
        {
            // Haven't received the full headers yet — wait for more data.
            break;
        }

        const int dataStart = headerEndPos + headerEnd.size();

        // Find the next boundary marker to determine the end of JPEG data.
        const int nextMarkerPos = m_buffer.indexOf(marker, dataStart);
        if (nextMarkerPos < 0)
        {
            // Haven't received the end of this frame yet — wait for more data.
            break;
        }

        // Ian: The JPEG data ends just before the next boundary.
        // There's usually a \r\n before the boundary marker — strip it.
        int dataEnd = nextMarkerPos;
        if (dataEnd >= 2 && m_buffer.at(dataEnd - 2) == '\r' && m_buffer.at(dataEnd - 1) == '\n')
        {
            dataEnd -= 2;
        }

        if (dataEnd > dataStart)
        {
            const QByteArray jpegData = m_buffer.mid(dataStart, dataEnd - dataStart);
            ProcessJpegFrame(jpegData);
        }

        // Ian: Remove everything up to (but not including) the next boundary
        // marker so we can find it again on the next iteration.
        m_buffer.remove(0, nextMarkerPos);
    }

    // Ian: Safety valve — if the buffer grows beyond a reasonable size without
    // producing frames, something is wrong (bad boundary, corrupted stream).
    // Discard old data to prevent unbounded memory growth.
    static constexpr int kMaxBufferSize = 10 * 1024 * 1024;  // 10 MB
    if (m_buffer.size() > kMaxBufferSize)
    {
        m_buffer.clear();
    }
}

bool MjpegStreamSource::ParseBoundaryFromContentType(const QString& contentType)
{
    // Ian: Content-Type header looks like:
    //   multipart/x-mixed-replace; boundary=--myboundary
    //   multipart/x-mixed-replace;boundary=myboundary
    //   multipart/x-mixed-replace; boundary="myboundary"
    // We need to extract "myboundary" (without leading dashes or quotes).

    if (!contentType.contains(QStringLiteral("multipart"), Qt::CaseInsensitive))
    {
        return false;
    }

    const int boundaryIdx = contentType.indexOf(QStringLiteral("boundary="), 0, Qt::CaseInsensitive);
    if (boundaryIdx < 0)
    {
        return false;
    }

    QString boundary = contentType.mid(boundaryIdx + 9).trimmed();

    // Strip quotes if present.
    if (boundary.startsWith('"') && boundary.endsWith('"'))
    {
        boundary = boundary.mid(1, boundary.size() - 2);
    }

    // Strip leading dashes — we'll add "--" ourselves when searching.
    while (boundary.startsWith('-'))
    {
        boundary.remove(0, 1);
    }

    if (boundary.isEmpty())
    {
        return false;
    }

    m_boundary = boundary.toLatin1();
    return true;
}

void MjpegStreamSource::ProcessJpegFrame(const QByteArray& jpegData)
{
    QImage frame;
    if (!frame.loadFromData(jpegData, "JPEG"))
    {
        // Ian: Silently skip corrupt frames.  This can happen if we
        // get a partial frame during stream startup or a network glitch.
        // Don't set an error state — the stream is still alive.
        return;
    }

    if (m_state != State::Streaming)
    {
        SetState(State::Streaming);
    }

    // Ian: FPS measurement — count frames in 1-second windows.
    ++m_frameCount;
    const qint64 elapsedMs = m_fpsTimer.elapsed();
    const qint64 windowMs = elapsedMs - m_lastFpsMeasureMs;
    if (windowMs >= kFpsMeasureIntervalMs)
    {
        m_fps = (static_cast<double>(m_frameCount) * 1000.0) / static_cast<double>(windowMs);
        m_frameCount = 0;
        m_lastFpsMeasureMs = elapsedMs;
    }

    emit FrameReady(frame);
}

void MjpegStreamSource::SetState(State newState)
{
    if (m_state == newState)
    {
        return;
    }
    m_state = newState;
    emit StateChanged(newState);
}

void MjpegStreamSource::SetError(const QString& message)
{
    m_lastError = message;
    SetState(State::Error);
}

}  // namespace sd::camera
