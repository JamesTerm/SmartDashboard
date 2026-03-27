/// @file camera_publisher_discovery.cpp
/// @brief Monitors NT4 /CameraPublisher/ keys to auto-discover cameras.

#include "camera/camera_publisher_discovery.h"

#include <QStringList>

namespace sd::camera
{

// Ian: The CameraPublisher key prefix.  All keys under this path are
// camera-related.  The schema is defined by WPILib's CameraServer class.
static const QString kCameraPublisherPrefix = QStringLiteral("/CameraPublisher/");
static const QString kStreamsKeySuffix = QStringLiteral("/streams");

CameraPublisherDiscovery::CameraPublisherDiscovery(QObject* parent)
    : QObject(parent)
{
}

void CameraPublisherDiscovery::OnVariableUpdate(const QString& key, int /*valueType*/, const QVariant& value)
{
    // Ian: We only care about /CameraPublisher/*/streams keys.
    // Other keys under /CameraPublisher/ (source, description, connected, mode)
    // are informational but don't affect stream discovery.
    if (!key.startsWith(kCameraPublisherPrefix))
    {
        return;
    }

    if (!key.endsWith(kStreamsKeySuffix))
    {
        return;
    }

    // Extract camera name from: /CameraPublisher/{CameraName}/streams
    const int nameStart = kCameraPublisherPrefix.size();
    const int nameEnd = key.indexOf('/', nameStart);
    if (nameEnd <= nameStart)
    {
        return;
    }

    const QString cameraName = key.mid(nameStart, nameEnd - nameStart);
    if (cameraName.isEmpty())
    {
        return;
    }

    const QStringList urls = ParseStreamUrls(value);

    // Ian: Only emit if the URLs actually changed to avoid redundant updates.
    if (m_cameras.value(cameraName) != urls)
    {
        m_cameras[cameraName] = urls;
        emit CameraDiscovered(cameraName, urls);
    }
}

void CameraPublisherDiscovery::Clear()
{
    m_cameras.clear();
    emit CamerasCleared();
}

QStringList CameraPublisherDiscovery::GetCameraNames() const
{
    return m_cameras.keys();
}

QStringList CameraPublisherDiscovery::GetStreamUrls(const QString& cameraName) const
{
    return m_cameras.value(cameraName);
}

QStringList CameraPublisherDiscovery::ParseStreamUrls(const QVariant& value)
{
    QStringList result;

    // Ian: The NT4 value for /streams is a string array.  QVariant from
    // our transport layer may deliver it as a QStringList, a QVariantList,
    // or a single string.  Handle all cases.
    if (value.typeId() == QMetaType::QStringList)
    {
        const QStringList rawUrls = value.toStringList();
        for (const QString& url : rawUrls)
        {
            const QString clean = StripStreamPrefix(url.trimmed());
            if (!clean.isEmpty())
            {
                result.append(clean);
            }
        }
    }
    else if (value.typeId() == QMetaType::QString)
    {
        const QString clean = StripStreamPrefix(value.toString().trimmed());
        if (!clean.isEmpty())
        {
            result.append(clean);
        }
    }
    else if (value.canConvert<QVariantList>())
    {
        const QVariantList list = value.toList();
        for (const QVariant& item : list)
        {
            const QString clean = StripStreamPrefix(item.toString().trimmed());
            if (!clean.isEmpty())
            {
                result.append(clean);
            }
        }
    }

    return result;
}

QString CameraPublisherDiscovery::StripStreamPrefix(const QString& url)
{
    // Ian: CameraPublisher stream URLs have a protocol prefix like "mjpg:"
    // that indicates the stream type.  Strip it to get the raw HTTP URL
    // that QNetworkAccessManager can consume.
    if (url.startsWith(QStringLiteral("mjpg:"), Qt::CaseInsensitive))
    {
        return url.mid(5);
    }
    // Future: could also handle "h264:" or other prefixes here.
    return url;
}

}  // namespace sd::camera
