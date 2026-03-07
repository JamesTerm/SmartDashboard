#pragma once

#include "sd_direct_subscriber.h"

#include <QObject>
#include <QVariant>

#include <memory>

class DirectSubscriberAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit DirectSubscriberAdapter(QObject* parent = nullptr);
    ~DirectSubscriberAdapter() override;

    bool Start();
    void Stop();

signals:
    void VariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void ConnectionStateChanged(int state);

private:
    std::unique_ptr<sd::direct::IDirectSubscriber> m_subscriber;
};
