#include "wampcrauser.h"
#include "credentialstore.h"
#include "wampconnection.h"
#include "user_p.h"
#include <QMessageAuthenticationCode>

namespace QFlow{

class WampCraUserPrivate : public UserPrivate
{
public:
    QString _secret;
    WampCraUserPrivate() : UserPrivate()
    {

    }
    ~WampCraUserPrivate()
    {

    }
};
WampCraUser::WampCraUser(QObject *parent) : User(*new WampCraUserPrivate, parent)
{

}
WampCraUser::WampCraUser(WampCraUserPrivate &dd, QObject *parent) : User(dd, parent)
{

}

WampCraUser::WampCraUser(QString name, QString secret, QObject* parent) : User(*new WampCraUserPrivate, parent)
{
    Q_D(WampCraUser);
    d->_secret = secret;
    setName(name);
}

WampCraUser::~WampCraUser()
{

}
QString WampCraUser::secret() const
{
    Q_D(const WampCraUser);
    return d->_secret;
}
void WampCraUser::setSecret(QString value)
{
    Q_D(WampCraUser);
    d->_secret = value;
}
QString WampCraUser::authMethod() const
{
    return "wampcra";
}
QByteArray WampCraUser::response(QByteArray challenge)
{
    Q_D(WampCraUser);
    QMessageAuthenticationCode hash(QCryptographicHash::Sha256);
    CredentialStore store;
    QString secret = d->_secret;
    if(secret.isEmpty())
    {
        WampConnection* parent = (WampConnection*)this->parent();
        secret = store.readPassword(parent->url()).toUtf8();
    }
    /*if(extra.contains("salt") && !_socketPrivate->_wampCraIsSalted)
    {
        QByteArray salted = WampConnectionPrivate::PBKDF2(secret,
                                                          extra["salt"].toString(), extra["iterations"].toInt());
        secret = salted.toBase64();
    }*/
    hash.setKey(secret.toLatin1());
    hash.addData(challenge);
    QByteArray hashResult = hash.result().toBase64();
    return hashResult;
}
}
