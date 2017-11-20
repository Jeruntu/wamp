#ifndef WAMPINVOCATION
#define WAMPINVOCATION

#include <QJsonArray>
#include <QSharedPointer>

namespace QFlow{

class Registration;
typedef QSharedPointer<Registration> RegistrationPointer;

class WampInvocation
{
public:
    RegistrationPointer registration;
    QVariantList args;
    qulonglong requestId;
    WampInvocation() : registration(NULL)
    {

    }
    ~WampInvocation()
    {

    }
};
typedef QSharedPointer<WampInvocation> WampInvocationPointer;
}
using namespace QFlow;
Q_DECLARE_METATYPE(WampInvocationPointer)
#endif // WAMPINVOCATION

