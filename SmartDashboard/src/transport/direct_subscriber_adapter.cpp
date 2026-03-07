#include "transport/direct_subscriber_adapter.h"

#include <QMetaObject>

DirectSubscriberAdapter::DirectSubscriberAdapter(QObject* parent)
    : QObject(parent)
{
    sd::direct::SubscriberConfig config;
    m_subscriber = sd::direct::CreateDirectSubscriber(config);
}

DirectSubscriberAdapter::~DirectSubscriberAdapter()
{
    Stop();
}

bool DirectSubscriberAdapter::Start()
{
    if (!m_subscriber)
    {
        return false;
    }

    return m_subscriber->Start(
        [this](const sd::direct::VariableUpdate& update)
        {
            QVariant value;
            switch (update.type)
            {
                case sd::direct::ValueType::Bool:
                    value = update.value.boolValue;
                    break;
                case sd::direct::ValueType::Double:
                    value = update.value.doubleValue;
                    break;
                case sd::direct::ValueType::String:
                    value = QString::fromStdString(update.value.stringValue);
                    break;
                default:
                    value = QVariant();
                    break;
            }

            QMetaObject::invokeMethod(this, [this, update, value]()
            {
                emit VariableUpdateReceived(
                    QString::fromStdString(update.key),
                    static_cast<int>(update.type),
                    value,
                    update.seq
                );
            }, Qt::QueuedConnection);
        },
        [this](sd::direct::ConnectionState state)
        {
            QMetaObject::invokeMethod(this, [this, state]()
            {
                emit ConnectionStateChanged(static_cast<int>(state));
            }, Qt::QueuedConnection);
        }
    );
}

void DirectSubscriberAdapter::Stop()
{
    if (m_subscriber)
    {
        m_subscriber->Stop();
    }
}
