#ifndef SUBSCRIPTION_P_H
#define SUBSCRIPTION_P_H

#include "functor.h"
#include "helper.h"
#include <QUrl>
#include <QJsonArray>
#include <QJSValue>
#include <QQmlListReference>
#include <QMetaMethod>
#include <QJsonObject>
#include <QJSEngine>

namespace QFlow{

class Subscription : public QObject
{
protected:
    qulonglong _subscriptionId;
    QString _uri;
public:
    Subscription() : _subscriptionId(-1)
    {

    }
    QString uri() const
    {
        return _uri;
    }
    void setSubscriptionId(qulonglong subscriptionId)
    {
        _subscriptionId = subscriptionId;
    }

    Subscription(QString uri) : _uri(uri)
    {

    }
    qulonglong subscriptionId() const
    {
        return _subscriptionId;
    }
    virtual ~Subscription()
    {

    }
    virtual void handle(const QVariantList& args, const QVariantMap& kwargs, const QVariantMap& details) = 0;
};
typedef QSharedPointer<Subscription> SubscriptionPointer;
class JSSubscription : public Subscription
{
    QJSValue _callback;
public:
    JSSubscription(QString uri, QJSValue val) : Subscription(uri), _callback(val)
    {
        Q_ASSERT(val.isCallable());
    }
    void handle(const QVariantList& args, const QVariantMap& kwargs, const QVariantMap& details) override
    {
        QJSValueList params;
        auto jsArgs = _callback.engine()->toScriptValue(args);
        auto jsKwargs = _callback.engine()->toScriptValue(kwargs);
        auto jsDetails = _callback.engine()->toScriptValue(details);
        params.append(jsArgs);
        params.append(jsKwargs);
        params.append(jsDetails);
        _callback.call(params);
    }

    ~JSSubscription()
    {

    }
};
class MethodSubscription : public Subscription
{
    QObject* _obj;
    QString _method;
    QMetaMethod _metaMethod;

public:
    MethodSubscription(QString uri, QObject* obj, QString method) : Subscription(uri), _obj(obj), _method(method)
    {
        int index = _obj->metaObject()->indexOfSlot(method.toLatin1());
        _metaMethod = _obj->metaObject()->method(index);
    }

    void handle(const QVariantList& args, const QVariantMap& kwargs, const QVariantMap& details) override
    {
        QGenericArgument genArgs{QMetaType::typeName(QMetaType::QVariantList), &args};
        QGenericArgument genKwargs{QMetaType::typeName(QMetaType::QVariantMap), &kwargs};
        QGenericArgument genDetails{QMetaType::typeName(QMetaType::QVariantMap), &details};
        _metaMethod.invoke(_obj, Qt::AutoConnection, genArgs, genKwargs, genDetails);
    }
};

class FunctorSubscription : public Subscription
{
    FunctorBase* _functor;
public:
    FunctorSubscription(QString uri, FunctorBase* functor) : Subscription(uri), _functor(functor)
    {

    }
    ~FunctorSubscription()
    {

    }
    void handle(const QVariantList& args, const QVariantMap& kwargs, const QVariantMap& details) override
    {
//        _functor->invoke(args, kwargs, details); // [JJO] To be continued
    }

};
}
#endif // SUBSCRIPTION_P_H

