#include "wampworker.h"
#include "wampconnection_p.h"
#include "wampconnection.h"
#include "credentialstore.h"
#include "wampinvocation.h"
#include "wampcrauser.h"
#include "helper.h"
#include "wampmessageserializer.h"
#include "websocketconnection.h"
#include "call.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>

namespace QFlow{
    
const char* socketStateToString(QAbstractSocket::SocketState state) {
    switch (state)
    {
        case QAbstractSocket::ConnectedState:
            return "CONNECTED";
        case QAbstractSocket::ConnectingState:
            return "CONNECTING";
        case QAbstractSocket::UnconnectedState:
            return "UNCONNECTED";
        case QAbstractSocket::HostLookupState:
            return "HOSTLOOKUP";
        case QAbstractSocket::BoundState:
            return "BOUND";
        case QAbstractSocket::ListeningState:
            return "LISTENING";
        case QAbstractSocket::ClosingState:
            return "CLOSING";
        default:
            return "UNCONNECTED";
    }
}

WampWorker::WampWorker() : QObject(), _timer(new QTimer(this))
{
    _timer->setInterval(5000);
    QObject::connect(_timer, &QTimer::timeout, this, &WampWorker::reconnect);
}
WampWorker::~WampWorker()
{
    disconnect();
}

void WampWorker::reconnect()
{
    if (!_socket.isNull())
    {
        _socket->close();
        connect();
    }
}

void WampWorker::connect()
{
    _timer->stop();
    _socket.reset(new WebSocketConnection());
    _socket->setUri(_socketPrivate->_url.url());
    _socket->setRequestedSubprotocols({KEY_WAMP_MSGPACK_SUB, KEY_WAMP_JSON_SUB}); //prefer msgpack over json as it's faster
    QObject::connect(_socket.data(), &WebSocketConnection::opened, this, &WampWorker::opened);
    QObject::connect(_socket.data(), &WebSocketConnection::closed, this, &WampWorker::closed);
    QObject::connect(_socket.data(), &WebSocketConnection::messageReceived, this, &WampWorker::messageReceived);
    _socket->connect();
    _timer->start();
}
    
void WampWorker::disconnect()
{
    if (!_socket.isNull())
    {
        _socket->close();
        _socket.reset();
    }
    _timer->stop();
}

void WampWorker::sendTextMessage(const QString &message)
{
    if (!_socket.isNull())
        _socket->sendText(message);
    else {
        qWarning() << "Invalid socket state while attempting to send text message [" << message << "]";
    }
}
void WampWorker::sendBinaryMessage(const QByteArray &message)
{
    if (!_socket.isNull())
        _socket->sendBinary(message);
    else
        qWarning() << "Attempting to send binary message while socket to WAMP server is closed";
}
void WampWorker::closed()
{
    qDebug() << "WampConnection: WebSocket closed";
    if (_socketPrivate)
        for(auto observer: _socketPrivate->_topicObserver.values())
        {
            observer->setEnabled(false);
        }
    if (_socketPrivate->q_ptr)
    {
        Q_EMIT _socketPrivate->q_ptr->disconnected();
    }
    if (!_socket.isNull())
        _timer->start();
}
    
void WampWorker::flush()
{
    QCoreApplication::processEvents();
}

void WampWorker::opened()
{
    _timer->stop();
    QString sub = _socket->subprotocol();
    _socketPrivate->_serializer.reset(WampMessageSerializer::create(sub));
    QVariantMap options;
	CredentialStore store;
	
    if (_socketPrivate->_user.isNull())
    {
        _socketPrivate->_user.reset(new WampCraUser());
        _socketPrivate->_user->setName(store.readUsername(_socketPrivate->_url));
    }
	
	QVariantList authMethods;
	if(!_socketPrivate->_user->name().isEmpty())
    {
        options["authid"] = _socketPrivate->_user->name();
		authMethods.append(_socketPrivate->_user->authMethod());
    }
	authMethods.append("anonymous");
	options["authmethods"] = authMethods;
	
	
    QVariantMap roles{{"publisher", QVariantMap()}, {"subscriber", QVariantMap()}, {"caller", QVariantMap()}, {"callee", QVariantMap()}};
    options["roles"] = roles;
    QVariantList arr{WampMsgCode::HELLO, _socketPrivate->_realm, options};
    _socketPrivate->sendWampMessage(arr);
}
void WampWorker::messageReceived(const QByteArray &message)
{
    if (_socketPrivate->q_ptr)
    {
    QVariantList arr = _socketPrivate->_serializer->deserialize(message);
    WampMsgCode code = (WampMsgCode)arr[0].toInt();
    if(code == WampMsgCode::ERROR)
    {
        WampMsgCode subCode = (WampMsgCode)arr[1].toInt();
        qulonglong requestId = arr[2].toULongLong();
        QUrl uri = arr[4].toString();
        if(subCode == WampMsgCode::CALL)
        {
            CallPointer call = _socketPrivate->_pendingCalls.take(requestId);
            call->resultReady(QVariant());
        }
        QVariantMap details = arr[3].toMap();
        QVariantList args;
        if(arr.count() > 5)
        {
            args = arr[5].toList();
        }
        WampError wampError((int)subCode, requestId, details, uri, args);
        Q_EMIT _socketPrivate->q_ptr->error(wampError);

    }
    if(code == WampMsgCode::ABORT)
    {
        QString uri = arr[2].toString();
        QVariantMap details = arr[1].toMap();
        WampError wampError((int)WampMsgCode::ABORT, 0, details, uri, QVariantList());
        Q_EMIT _socketPrivate->q_ptr->error(wampError);
    }
    if(code == WampMsgCode::WELCOME)
    {
        _socketPrivate->onConnected();
    }
    else if(code == WampMsgCode::REGISTERED)
    {
        qulonglong regId = arr[2].toULongLong();
        qulonglong requestId = arr[1].toULongLong();
        RegistrationPointer reg = _socketPrivate->_pendingRegistrations.take(requestId);
        reg->setRegistrationId(regId);
        if(_socketPrivate->_uriRegistration.contains(reg->uri()))
        {
            RegistrationPointer oldReg = _socketPrivate->_uriRegistration.take(reg->uri());
            _socketPrivate->_registrations.remove(oldReg->registrationId());
        }
        _socketPrivate->_registrations[regId] = reg;
        _socketPrivate->_uriRegistration[reg->uri()] = reg;
    }
    else if(code == WampMsgCode::UNREGISTERED){
        qulonglong requestId = arr[1].toULongLong();
        RegistrationPointer reg = _socketPrivate->_pendingUnregistrations.take(requestId);
        _socketPrivate->_registrations.remove(reg->registrationId());
        _socketPrivate->_uriRegistration.remove(reg->uri());

    }
    else if(code == WampMsgCode::SUBSCRIBED)
    {
        qulonglong subId = arr[2].toULongLong();
        qulonglong requestId = arr[1].toULongLong();
        SubscriptionPointer sub = _socketPrivate->_pendingSubscriptions.take(requestId);
        sub->setSubscriptionId(subId);
        if(_socketPrivate->_uriSubscription.contains(sub->uri()))
        {
            SubscriptionPointer oldSub = _socketPrivate->_uriSubscription.take(sub->uri());
            _socketPrivate->_subscriptions.remove(oldSub->subscriptionId());
        }
        _socketPrivate->_subscriptions[subId] = sub;
        _socketPrivate->_uriSubscription[sub->uri()] = sub;
    }
    else if(code == WampMsgCode::UNSUBSCRIBED){
        qulonglong requestId = arr[1].toULongLong();
        SubscriptionPointer sub = _socketPrivate->_pendingUnsubscriptions.take(requestId);
        _socketPrivate->_subscriptions.remove(sub->subscriptionId());
        _socketPrivate->_uriSubscription.remove(sub->uri());
    }
    else if(code == WampMsgCode::INVOCATION)
    {
        qulonglong regId = arr[2].toULongLong();
        RegistrationPointer reg = _socketPrivate->_registrations[regId];
        QVariantList args;
        if(arr.count() > 4 && (QMetaType::Type)arr[4].type() == QMetaType::QVariantList)
        {
            args = arr[4].toList();
        }

        WampInvocationPointer inv(new WampInvocation(), InvocationDeleter());
        inv->registration = reg;
        inv->args = args;
        inv->requestId = arr[1].toULongLong();
        QMetaObject::invokeMethod(_socketPrivate, "handleInvocation", Qt::QueuedConnection, Q_ARG(WampInvocationPointer, inv));
    }
    else if(code == WampMsgCode::EVENT)
    {
        qulonglong subId = arr[1].toULongLong();
        if(!_socketPrivate->_subscriptions.contains(subId))
        {
            qDebug() << "Event received for non existing subscritption " << subId;
            return;
        }
        SubscriptionPointer sub = _socketPrivate->_subscriptions[subId];
        QVariantList args;
        if(arr.count() > 4 && (QMetaType::Type)arr[4].type() == QMetaType::QVariantList)
        {
            args = arr[4].toList();
        }
        Event event;
        event.subscription = sub;
        event.args = args;
        event.publicationId = arr[2].toULongLong();
        QMetaObject::invokeMethod(_socketPrivate, "handleEvent", Qt::QueuedConnection, Q_ARG(Event, event));

    }
    else if(code == WampMsgCode::RESULT)
    {
        qulonglong requestId = arr[1].toULongLong();
        CallPointer call = _socketPrivate->_pendingCalls.take(requestId);
        QVariant result;
        if(arr.count() > 3)
        {
            QVariantList resultArray = arr[3].toList();
            result = resultArray[0];
        }
        call->resultReady(result);
    }
    else if(code == WampMsgCode::PUBLISHED)
    {
    }
    else if(code == WampMsgCode::CHALLENGE)
    {
        QVariantMap extra = arr[2].toMap();
        QString result = QString(_socketPrivate->_user->response(extra["challenge"].toString().toLatin1()));
        QVariantList resArr{WampMsgCode::AUTHENTICATE, result, QVariantMap()};
        _socketPrivate->sendWampMessage(resArr);
    }
    else if (code == WampMsgCode::GOODBYE)
    {
        QVariantMap details = arr[1].toMap();
        QString reason = arr[2].toString();
        qInfo() << "Received GOODBYE msg with reason: " << reason << " and details: " << details;
        if (_socketPrivate->q_ptr)
            Q_EMIT _socketPrivate->q_ptr->disconnected();
        disconnect();
    }
    if (_socketPrivate->q_ptr)
        Q_EMIT _socketPrivate->q_ptr->textMessageReceived(message);
    }
}
}
