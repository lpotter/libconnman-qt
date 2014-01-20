/*
 * Copyright © 2010, Intel Corporation.
 * Copyright © 2012, Jolla.
 *
 * This program is licensed under the terms and conditions of the
 * Apache License, version 2.0.  The full text of the Apache License is at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 */

#include "networkmanager.h"

#include "commondbustypes.h"
#include "connman_manager_interface.h"
#include "connman_manager_interface.cpp" // not bug
#include "moc_connman_manager_interface.cpp" // not bug

static NetworkManager* staticInstance = NULL;

NetworkManager* NetworkManagerFactory::createInstance()
{
    if(!staticInstance)
        staticInstance = new NetworkManager;

    return staticInstance;
}

NetworkManager* NetworkManagerFactory::instance()
{
    return createInstance();
}

// NetworkManager implementation

const QString NetworkManager::State("State");
const QString NetworkManager::OfflineMode("OfflineMode");
const QString NetworkManager::SessionMode("SessionMode");

NetworkManager::NetworkManager(QObject* parent)
  : QObject(parent),
    m_manager(NULL),
    m_defaultRoute(NULL),
    m_invalidDefaultRoute(NULL),
    watcher(NULL),
    m_available(false),
    m_servicesEnabled(true),
    m_technologiesEnabled(true),
    servicesReady(false),
    technologiesReady(false)
{
    registerCommonDataTypes();

    watcher = new QDBusServiceWatcher("net.connman",QDBusConnection::systemBus(),
            QDBusServiceWatcher::WatchForRegistration |
            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, SIGNAL(serviceRegistered(QString)),
            this, SLOT(connectToConnman(QString)));
    connect(watcher, SIGNAL(serviceUnregistered(QString)),
            this, SLOT(connmanUnregistered(QString)));

    m_available = QDBusConnection::systemBus().interface()->isServiceRegistered("net.connman");

    if(m_available)
        connectToConnman();
    else
        qDebug() << "connman not AVAILABLE";
}

NetworkManager::~NetworkManager()
{
}

void NetworkManager::connectToConnman(QString)
{
    disconnectFromConnman();
    m_manager = new NetConnmanManagerInterface("net.connman", "/",
            QDBusConnection::systemBus(), this);

    if (!m_manager->isValid()) {

        delete m_manager;
        m_manager = NULL;

        // shouldn't happen but in this case service isn't available
        if(m_available)
            Q_EMIT availabilityChanged(m_available = false);
    } else {

        QDBusPendingReply<QVariantMap> props_reply = m_manager->GetProperties();
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(props_reply, this);
        connect(watcher,
                SIGNAL(finished(QDBusPendingCallWatcher*)),
                this,
                SLOT(propertiesReply(QDBusPendingCallWatcher*)));
        updateDefaultRoute();

    }
}

void NetworkManager::disconnectFromConnman(QString)
{
    delete m_manager;
    m_manager = NULL;

    disconnectTechnologies();
    disconnectServices();
}

void NetworkManager::disconnectTechnologies()
{
    if (m_manager) {
        disconnect(m_manager, SIGNAL(TechnologyAdded(QDBusObjectPath,QVariantMap)),
                   this, SLOT(technologyAdded(QDBusObjectPath,QVariantMap)));
        disconnect(m_manager, SIGNAL(TechnologyRemoved(QDBusObjectPath)),
                   this, SLOT(technologyRemoved(QDBusObjectPath)));
    }

    Q_FOREACH (NetworkTechnology *tech, m_technologiesCache)
        tech->deleteLater();

    if (!m_technologiesCache.isEmpty()) {
        m_technologiesCache.clear();
        Q_EMIT technologiesChanged();
    }
}

void NetworkManager::disconnectServices()
{
    if (m_manager) {
        disconnect(m_manager, SIGNAL(ServicesChanged(ConnmanObjectList,QList<QDBusObjectPath>)),
                   this, SLOT(updateServices(ConnmanObjectList,QList<QDBusObjectPath>)));
        disconnect(m_manager, SIGNAL(SavedServicesChanged(ConnmanObjectList)),
                   this, SLOT(updateSavedServices(ConnmanObjectList)));
    }

    Q_FOREACH (NetworkService *service, m_servicesCache)
        service->deleteLater();

    m_servicesCache.clear();

    if (m_defaultRoute != m_invalidDefaultRoute) {
        m_defaultRoute = m_invalidDefaultRoute;
        Q_EMIT defaultRouteChanged(m_defaultRoute);
    }

    if (!m_servicesOrder.isEmpty()) {
        m_servicesOrder.clear();
        Q_EMIT servicesChanged();
    }

    if (!m_savedServicesOrder.isEmpty()) {
        m_savedServicesOrder.clear();
        Q_EMIT savedServicesChanged();
    }
}

void NetworkManager::connmanUnregistered(QString)
{
    disconnectFromConnman();

    if(m_available)
        Q_EMIT availabilityChanged(m_available = false);
}

void NetworkManager::setupTechnologies()
{
    QDBusPendingReply<ConnmanObjectList> reply = m_manager->GetTechnologies();
    // propertiesReply
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            this,
            SLOT(technologiesReply(QDBusPendingCallWatcher*)));

    connect(m_manager,
            SIGNAL(TechnologyAdded(const QDBusObjectPath &, const QVariantMap &)),
            this,
            SLOT(technologyAdded(const QDBusObjectPath &, const QVariantMap &)));

    connect(m_manager,
            SIGNAL(TechnologyRemoved(const QDBusObjectPath &)),
            this,
            SLOT(technologyRemoved(const QDBusObjectPath &)));
}

void NetworkManager::setupServices()
{
    qDebug() << "<<<<<<<<<<<<<<<<<<<<<<<<<<";
    QDBusPendingReply<ConnmanObjectList> reply = m_manager->GetServices();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            this,
            SLOT(servicesReply(QDBusPendingCallWatcher*)));

    connect(m_manager,
            SIGNAL(ServicesChanged(ConnmanObjectList, QList<QDBusObjectPath>)),
            this,
            SLOT(updateServices(ConnmanObjectList, QList<QDBusObjectPath>)));

    connect(m_manager,
            SIGNAL(SavedServicesChanged(ConnmanObjectList)),
            this,
            SLOT(updateSavedServices(ConnmanObjectList)));

}

void NetworkManager::updateServices(const ConnmanObjectList &changed, const QList<QDBusObjectPath> &removed)
{
    ConnmanObject connmanobj;
    int order = -1;
    NetworkService *service = NULL;

    // make sure we don't leak memory
    m_servicesOrder.clear();

    QStringList serviceList;
    Q_FOREACH (connmanobj, changed) {
        order++;
        bool addedService = false;

        const QString svcPath(connmanobj.objpath.path());

        if (!m_servicesCache.contains(svcPath)) {
            service = new NetworkService(svcPath,
                                         connmanobj.properties, this);
            m_servicesCache.insert(svcPath, service);
            addedService = true;
        } else {
            service = m_servicesCache.value(svcPath);
            service->updateProperties(connmanobj.properties);
        }

        m_servicesOrder.push_back(service);
        serviceList.push_back(service->path());

        // If this is no longer a favorite network, remove it from the saved list
        if (!service->favorite()) {
            int savedIndex;
            if ((savedIndex = m_savedServicesOrder.indexOf(service)) != -1) {
                m_savedServicesOrder.remove(savedIndex);
            }
        }
        if (order == 0)
            updateDefaultRoute();

        if (addedService) { //Q_EMIT this after m_servicesOrder is updated
            Q_EMIT serviceAdded(svcPath);
        }
    }

    Q_FOREACH (QDBusObjectPath obj, removed) {
        const QString svcPath(obj.path());
        if (m_servicesCache.contains(svcPath)) {
            if (NetworkService *service = m_servicesCache.value(svcPath)) {
                if (m_savedServicesOrder.contains(service)) {
                    // Don't remove this service from the cache, since the saved model needs it
                    // Update the strength value to zero, so we know it isn't visible
                    QVariantMap properties;
                    properties.insert(QString::fromLatin1("Strength"), QVariant(static_cast<quint32>(0)));
                    properties.insert(QLatin1String("State"), QLatin1String("idle"));
                    service->updateProperties(properties);
                } else {
                    service->deleteLater();
                    m_servicesCache.remove(svcPath);
                }
                Q_EMIT serviceRemoved(svcPath);
            }
        } else {
            // connman maintains a virtual "hidden" wifi network and removes it upon init
            qDebug() << "attempted to remove non-existing service";
        }
    }

    if (order == -1)
        updateDefaultRoute();
    Q_EMIT servicesChanged();
    Q_EMIT servicesListChanged(serviceList);

    Q_EMIT savedServicesChanged();
}

void NetworkManager::updateSavedServices(const ConnmanObjectList &changed)
{
    ConnmanObject connmanobj;
    int order = -1;
    NetworkService *service = NULL;

    // make sure we don't leak memory
    m_savedServicesOrder.clear();

    Q_FOREACH (connmanobj, changed) {
        order++;

        const QString svcPath(connmanobj.objpath.path());

        QHash<QString, NetworkService *>::iterator it = m_servicesCache.find(svcPath);
        if (it == m_servicesCache.end()) {
            service = new NetworkService(svcPath,
                                         connmanobj.properties, this);
            m_servicesCache.insert(svcPath, service);
        } else {
            service = *it;
            service->updateProperties(connmanobj.properties);
        }

        m_savedServicesOrder.push_back(service);
    }

    Q_EMIT savedServicesChanged();
}

void NetworkManager::propertyChanged(const QString &name,
                                     const QDBusVariant &value)
{
    QVariant tmp = value.variant();

    m_propertiesCache[name] = tmp;
    if (name == State) {
        QString stateString = tmp.toString();
        Q_EMIT stateChanged(stateString);
        updateDefaultRoute();
    } else if (name == OfflineMode) {
        Q_EMIT offlineModeChanged(tmp.toBool());
    } else if (name == SessionMode) {
       Q_EMIT sessionModeChanged(tmp.toBool());
   }
}

void NetworkManager::updateDefaultRoute()
{
    QString defaultNetDev;
    QFile routeFile("/proc/net/route");
    bool ok = false;
    if (routeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&routeFile);
        QString line = in.readLine();
        while (!line.isNull()) {
            QStringList lineList = line.split('\t');
            if (lineList.at(1) == "00000000" && lineList.at(3) == "0003") {
                defaultNetDev = lineList.at(0);
                break;
            }
            line = in.readLine();
        }
        routeFile.close();
    }

    Q_FOREACH(NetworkService *service, getServices("")) {
        if (service->state() == "online" || service->state() == "ready") {
            if (defaultNetDev == service->ethernet().value("Interface")) {
                if (m_defaultRoute != service) {
                    m_defaultRoute = service;
                    Q_EMIT defaultRouteChanged(m_defaultRoute);
                }
                return;
            }
        }
    }
    if (!m_invalidDefaultRoute)
        m_invalidDefaultRoute = new NetworkService("/", QVariantMap(), this);

    m_defaultRoute = m_invalidDefaultRoute;
    Q_EMIT defaultRouteChanged(m_defaultRoute);
}

void NetworkManager::technologyAdded(const QDBusObjectPath &technology,
                                     const QVariantMap &properties)
{
    NetworkTechnology *tech = new NetworkTechnology(technology.path(),
                                                    properties, this);

    m_technologiesCache.insert(tech->type(), tech);
    Q_EMIT technologiesChanged();
}

void NetworkManager::technologyRemoved(const QDBusObjectPath &technology)
{
    NetworkTechnology *net;
    // if we weren't storing by type() this loop would be unecessary
    // but since this function will be triggered rarely that's fine
    Q_FOREACH (net, m_technologiesCache) {
        if (net->objPath() == technology.path()) {
            m_technologiesCache.remove(net->type());
            net->deleteLater();
            break;
        }
    }

    Q_EMIT technologiesChanged();
}


// Public API /////////////

// Getters


bool NetworkManager::isAvailable() const
{
    return m_available;
}


const QString NetworkManager::state() const
{
    return m_propertiesCache[State].toString();
}

bool NetworkManager::offlineMode() const
{
    return m_propertiesCache[OfflineMode].toBool();
}

NetworkService* NetworkManager::defaultRoute() const
{
    return m_defaultRoute;
}

NetworkTechnology* NetworkManager::getTechnology(const QString &type) const
{
    if (m_technologiesCache.contains(type))
        return m_technologiesCache.value(type);
    else {
        qDebug() << "Technology " << type << " doesn't exist";
        return NULL;
    }
}

const QVector<NetworkTechnology *> NetworkManager::getTechnologies() const
{
    QVector<NetworkTechnology *> techs;

    Q_FOREACH (NetworkTechnology *tech, m_technologiesCache) {
        techs.push_back(tech);
    }

    return techs;
}

const QVector<NetworkService*> NetworkManager::getServices(const QString &tech) const
{
    QVector<NetworkService *> services;

    // this Q_FOREACH is based on the m_servicesOrder to keep connman's sort
    // of services.
    Q_FOREACH (NetworkService *service, m_servicesOrder) {
        if (tech.isEmpty() || service->type() == tech)
            services.push_back(service);
    }

    return services;
}

const QVector<NetworkService*> NetworkManager::getSavedServices(const QString &tech) const
{
    QVector<NetworkService *> services;

    // this Q_FOREACH is based on the m_servicesOrder to keep connman's sort
    // of services.
    Q_FOREACH (NetworkService *service, m_savedServicesOrder) {
        // A previously-saved network which is then removed, remains saved with favorite == false
        if ((tech.isEmpty() || service->type() == tech) && service->favorite())
            services.push_back(service);
    }

    return services;
}

// Setters

void NetworkManager::setOfflineMode(const bool &offlineMode)
{
    if(!m_manager) return;

    QDBusPendingReply<void> reply =
        m_manager->SetProperty(OfflineMode,
                               QDBusVariant(QVariant(offlineMode)));
}

  // these shouldn't crash even if connman isn't available
void NetworkManager::registerAgent(const QString &path)
{
    if(m_manager)
        m_manager->RegisterAgent(QDBusObjectPath(path));
}

void NetworkManager::unregisterAgent(const QString &path)
{
    if(m_manager)
        m_manager->UnregisterAgent(QDBusObjectPath(path));
}

void NetworkManager::registerCounter(const QString &path, quint32 accuracy,quint32 period)
{
    if(m_manager)
        m_manager->RegisterCounter(QDBusObjectPath(path),accuracy, period);
}

void NetworkManager::unregisterCounter(const QString &path)
{
    if(m_manager)
        m_manager->UnregisterCounter(QDBusObjectPath(path));
}

QDBusObjectPath NetworkManager::createSession(const QVariantMap &settings, const QString &sessionNotifierPath)
{
    if(!m_manager)
        return QDBusObjectPath();

    QDBusPendingReply<QDBusObjectPath> reply =
        m_manager->CreateSession(settings,QDBusObjectPath(sessionNotifierPath));
    reply.waitForFinished();
    return reply.value();
}

void NetworkManager::destroySession(const QString &sessionAgentPath)
{
    if(m_manager)
        m_manager->DestroySession(QDBusObjectPath(sessionAgentPath));
}

void NetworkManager::setSessionMode(const bool &sessionMode)
{
    if(m_manager)
        m_manager->SetProperty(SessionMode, QDBusVariant(sessionMode));
}

bool NetworkManager::sessionMode() const
{
    return m_propertiesCache[SessionMode].toBool();
}

bool NetworkManager::servicesEnabled() const
{
    return m_servicesEnabled;
}

void NetworkManager::setServicesEnabled(bool enabled)
{
    if (m_servicesEnabled == enabled)
        return;

    m_servicesEnabled = enabled;

    if (m_servicesEnabled)
        setupServices();
    else
        disconnectServices();

    Q_EMIT servicesEnabledChanged();
}

bool NetworkManager::technologiesEnabled() const
{
    return m_technologiesEnabled;
}

void NetworkManager::setTechnologiesEnabled(bool enabled)
{
    if (m_technologiesEnabled == enabled)
        return;

    m_technologiesEnabled = enabled;

    if (m_technologiesEnabled)
        setupTechnologies();
    else
        disconnectTechnologies();

    Q_EMIT technologiesEnabledChanged();
}

void NetworkManager::resetCountersForType(const QString &type)
{
    m_manager->ResetCounters(type);
}

QStringList NetworkManager::servicesList(const QString &tech)
{
    QStringList services;
    Q_FOREACH (NetworkService *service, m_servicesOrder) {
        if (tech.isEmpty() || service->type() == tech)
            services.push_back(service->path());
    }
    return services;
}

QString NetworkManager::technologyPathForService(const QString &servicePath)
{
    Q_FOREACH (NetworkService *service, m_servicesOrder) {
        if (service->path() == servicePath)
            return service->path();
    }
    return QString();
}
QString NetworkManager::technologyPathForType(const QString &techType)
{
    Q_FOREACH (NetworkTechnology *tech, m_technologiesCache) {
        if (tech->type() == techType)
            return tech->path();
    }
    return QString();
}

QStringList NetworkManager::technologiesList()
{
    QStringList techList;
    Q_FOREACH (NetworkTechnology *tech, m_technologiesCache) {
        techList << tech->type();
    }
    return techList;
}

void NetworkManager::propertiesReply(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<QVariantMap> props_reply = *call;
    qDebug() << props_reply.isError();

    if (props_reply.isError()) {
        qDebug() << props_reply.error().message();
        return;
    }
    m_propertiesCache = props_reply.value();

    Q_EMIT stateChanged(m_propertiesCache[State].toString());

    connect(m_manager,
            SIGNAL(PropertyChanged(const QString&, const QDBusVariant&)),
            this,
            SLOT(propertyChanged(const QString&, const QDBusVariant&)));

    if (m_technologiesEnabled)
        setupTechnologies();

    callsFinished();
//    if(!m_available)
//        Q_EMIT availabilityChanged(m_available = true);

}

void NetworkManager::technologiesReply(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<ConnmanObjectList> reply = *call;
    qDebug() << reply.isError();

    if (reply.isError()) {
        return;
    }
    ConnmanObjectList lst = reply.value();
    ConnmanObject obj;
    Q_FOREACH (obj, lst) { // TODO: consider optimizations

        NetworkTechnology *tech = new NetworkTechnology(obj.objpath.path(),
                                                        obj.properties, this);

        m_technologiesCache.insert(tech->type(), tech);
    }

    Q_EMIT technologiesChanged();
    call->deleteLater();
    technologiesReady = true;
    if (m_servicesEnabled)
        setupServices();
    callsFinished();
}

void NetworkManager::servicesReply(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<ConnmanObjectList> reply = *call;

    qDebug() << reply.isError();
    if (reply.isError()) {
        qDebug() << reply.error().message();
        return;
    }

    ConnmanObjectList lst = reply.value();
    ConnmanObject obj;
    int order = -1;
    NetworkService *service = NULL;

    // make sure we don't leak memory
    m_servicesOrder.clear();
    qDebug() << "services list" << lst.count();
    Q_FOREACH (obj, lst) { // TODO: consider optimizations
        order++;

        const QString svcPath(obj.objpath.path());

        service = new NetworkService(svcPath,
                                     obj.properties, this);

        m_servicesCache.insert(svcPath, service);
        m_servicesOrder.push_back(service);

        // by connman's documentation, first service is always
        // the default route's one
        if (order == 0)
            updateDefaultRoute();
    }

    // if no service was replied
    if (order == -1)
        updateDefaultRoute();

    Q_EMIT servicesChanged();

    // Find the saved services
    reply = m_manager->GetSavedServices();
    reply.waitForFinished();
    qDebug() << reply.isError();

    if (reply.isError()) {
        qDebug() << reply.error().message();
        return;
    }

    lst = reply.value();
    order = -1;
    service = NULL;

    m_savedServicesOrder.clear();
    qDebug() << "saved services list"<< lst.count();
    Q_FOREACH (obj, lst) {
        order++;

        const QString svcPath(obj.objpath.path());

        QHash<QString, NetworkService *>::iterator it = m_servicesCache.find(svcPath);
        if (it != m_servicesCache.end()) {
            service = *it;
        } else {
            service = new NetworkService(svcPath,
                                         obj.properties, this);
            m_servicesCache.insert(svcPath, service);
        }

        m_savedServicesOrder.push_back(service);
    }

    Q_EMIT savedServicesChanged();

    call->deleteLater();
    servicesReady = true;
    callsFinished();
}

void NetworkManager::callsFinished()
{
    qDebug() << m_technologiesEnabled << technologiesReady
                << m_servicesEnabled << servicesReady;

    if ((m_technologiesEnabled && !technologiesReady)
            || (m_servicesEnabled && !servicesReady)) {
        qDebug() << "just returning";
        return;
    }
    if(!m_available)
        Q_EMIT availabilityChanged(m_available = true);

//    Q_FOREACH(const QString &name,m_propertiesCache.keys()) {
//        emitPropertyChange(name,m_propertiesCache[name]);
//    }

}
