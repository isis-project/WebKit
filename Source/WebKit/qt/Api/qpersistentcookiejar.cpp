/*
    Copyright (C) 2011 Hewlett-Packard Development Company, L.P.
    Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include <config.h>
#include <QDir>
#include <QTextStream>
#include <QtDebug>
#include <QDateTime>
#include <wtf/text/CString.h>
#include <wtf/unicode/UTF8.h>
#include <wtf/unicode/Unicode.h>
#include "SQLiteDatabase.h"
#include "SQLiteStatement.h"
#include "SQLiteTransaction.h"
#include "qpersistentcookiejar.h"
#include <QSettings>
#include <QNetworkCookieJar>

static const QString DEFAULT_COOKIE_JAR_PATH = QLatin1String("/etc/palm/data/Browser/cookies");
static const QString DEFAULT_APP_ID = QLatin1String("browser-app");
static const QString QSETTINGS_COOKIEJAR_PATH_KEY = QLatin1String("CookieJarPath");
extern bool qIsEffectiveTLD(const QString &domain);

using namespace WebCore;
//This function is copied from QNetworkCookieJar
static inline bool isParentPath(QString path, QString reference)
{
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    if (!reference.endsWith(QLatin1Char('/')))
        reference += QLatin1Char('/');
    return path.startsWith(reference);
}

//This function is copied from QNetworkCookieJar
static inline bool isParentDomain(QString domain, QString reference)
{
    if (!reference.startsWith(QLatin1Char('.')))
        return domain == reference;

    return domain.endsWith(reference) || domain == reference.mid(1);
}

QPersistentCookieJar::QPersistentCookieJar(QObject* parent,const QString appId)
    : QNetworkCookieJar(parent)
    , m_dbReady(false)
    , m_enableCookies(true)
    , m_appId(DEFAULT_APP_ID)
    , m_dbPath(QLatin1String(""))
    , m_db(0)
    , m_insertCookieStmt(0)
    , m_selectCookieStmt(0)
    , m_deleteCookieStmt(0)
{
    if(!appId.isEmpty())
        m_appId = appId;
    QSettings appSettings;
    m_dbPath = appSettings.value(QSETTINGS_COOKIEJAR_PATH_KEY,DEFAULT_COOKIE_JAR_PATH).toString();

    if(!initDb() || !deleteSessionAndExpiredCookies())
        qDebug()<<"QPersistentCookieJar: sqlite3 DB Init/Load Failed...\n";
    else
        m_dbReady = true;
}

QPersistentCookieJar::~QPersistentCookieJar()
{
    deleteSessionAndExpiredCookies();
    closeDb();
}

bool QPersistentCookieJar::initDb()
{
    if (m_db)
        return false;

    if(!QDir().mkpath(m_dbPath))
        return false;

    m_db = new SQLiteDatabase();
    if (!m_db)
        return false;

    QString dbFileName;
    QTextStream(&dbFileName) << m_dbPath << "/" << m_appId << ".db";

    if (!m_db->open(dbFileName.toAscii().data())) {
        closeDb();
        return false;
    }

    if (!isValidDb()) {
        QFile dbFile(dbFileName);
        dbFile.remove();
        if (!m_db->open(dbFileName.toAscii().data()))
            closeDb();
        m_db->clearAllTables();
        if (!createTables()) {
            closeDb();
            return false;
        }
    }

    m_insertCookieStmt = new SQLiteStatement(*m_db, "INSERT INTO Cookies VALUES "
                                             "((?), (?), (?), (?), (?), (?), (?))");
    if (!m_insertCookieStmt || (m_insertCookieStmt->prepare() != SQLResultOk)) {
        closeDb();
        return false;
    }

    m_selectCookieStmt = new SQLiteStatement(*m_db, "SELECT domain, path, name, value, expires, secure, httponly FROM Cookies ");
    if (!m_selectCookieStmt || (m_selectCookieStmt->prepare() != SQLResultOk)) {
        closeDb();
        return false;
    }

    m_deleteCookieStmt = new SQLiteStatement(*m_db,"DELETE FROM Cookies WHERE domain = (?) AND path = (?) AND name = (?)");
    if (!m_deleteCookieStmt || (m_deleteCookieStmt->prepare() != SQLResultOk)) {
        closeDb();
        return false;
    }

    m_db->executeCommand("PRAGMA synchronous = OFF;");
    return true;

}

bool QPersistentCookieJar::isValidDb()
{
    SQLiteStatement integrityCheckStmt(*m_db, "PRAGMA integrity_check");
    int sqlResult = integrityCheckStmt.prepareAndStep();
    if (sqlResult != SQLResultRow || integrityCheckStmt.getColumnText(0) != "ok" || !m_db->tableExists("Cookies"))
        return false;
    return true;
}

bool QPersistentCookieJar::createTables()
{
    if (!m_db->executeCommand("CREATE TABLE Cookies "
                              "(domain TEXT NOT NULL ON CONFLICT FAIL,"
                              " path TEXT NOT NULL ON CONFLICT FAIL,"
                              " name TEXT NOT NULL ON CONFLICT FAIL,"
                              " value TEXT,"
                              " expires INTEGER NOT NULL ON CONFLICT FAIL,"
                              " secure INTEGER NOT NULL ON CONFLICT FAIL,"
                              " httponly INTEGER NOT NULL ON CONFLICT FAIL,"
                              " UNIQUE (domain, path, name) ON CONFLICT REPLACE); "))
        return false;
    if (!m_db->executeCommand("CREATE INDEX domainIndex ON Cookies (domain)"))
        return false;
    return true;
}

void QPersistentCookieJar::closeDb()
{
    delete m_insertCookieStmt;
    m_insertCookieStmt = 0;

    delete m_selectCookieStmt;
    m_selectCookieStmt = 0;

    delete m_deleteCookieStmt;
    m_deleteCookieStmt = 0;

    if (m_db)
        m_db->close();
    m_dbReady = false;
}

// This function logic is copied from QNetworkCookieJar::setCookiesFromUrl() and modified to handle DB requests
bool QPersistentCookieJar::setCookiesFromUrl(const QList<QNetworkCookie> &cookieList,
                                          const QUrl &url)
{
    if (!m_enableCookies || !m_dbReady)
        return false;

    QString defaultDomain = url.host();
    QString pathAndFileName = url.path();
    QString defaultPath = pathAndFileName.left(pathAndFileName.lastIndexOf(QLatin1Char('/'))+1);
    int cookiesAdded=0;
    if (defaultPath.isEmpty())
        defaultPath = QLatin1Char('/');

    QDateTime now = QDateTime::currentDateTimeUtc();
    foreach (QNetworkCookie cookie, cookieList) {
        if (cookie.path().isEmpty())
            cookie.setPath(defaultPath);

        if (cookie.domain().isEmpty()) {
            cookie.setDomain(defaultDomain);
        } else {
            // Ensure the domain starts with a dot if its field was not empty
            // in the HTTP header. There are some servers that forget the
            // leading dot and this is actually forbidden according to RFC 2109,
            // but all browsers accept it anyway so we do that as well.
            if (!cookie.domain().startsWith(QLatin1Char('.')))
                cookie.setDomain(QLatin1Char('.') + cookie.domain());

            QString domain = cookie.domain();
            if (!(isParentDomain(domain, defaultDomain)
                || isParentDomain(defaultDomain, domain)))
                continue; // not accepted

            // the check for effective TLDs makes the "embedded dot" rule from RFC 2109 section 4.3.2
            // redundant; the "leading dot" rule has been relaxed anyway, see above
            // we remove the leading dot for this check
            //FIXME: this function is internal function to QtCore lib and not exported through library
            if (qIsEffectiveTLD(domain.remove(0, 1)))
                continue; // not accepted
        }

        SQLiteTransaction deleteCookieTran(*m_db);
        m_deleteCookieStmt->prepare();
        deleteCookieTran.begin();
        m_deleteCookieStmt->bindText(1, cookie.domain());
        m_deleteCookieStmt->bindText(2, cookie.path());
        m_deleteCookieStmt->bindText(3, cookie.name().constData());
        m_deleteCookieStmt->step();
        m_deleteCookieStmt->reset();
        m_deleteCookieStmt->finalize();
        deleteCookieTran.commit();

        m_insertCookieStmt->prepare();
        m_insertCookieStmt->bindText(1, cookie.domain());
        m_insertCookieStmt->bindText(2, cookie.path());
        m_insertCookieStmt->bindText(3, cookie.name().constData());
        m_insertCookieStmt->bindText(4, cookie.value().constData());
        uint expirationDt = cookie.isSessionCookie() ? 0 : cookie.expirationDate().toTime_t();
        m_insertCookieStmt->bindInt64(5, expirationDt);
        m_insertCookieStmt->bindInt64(6, cookie.isSecure());
        m_insertCookieStmt->bindInt64(7, cookie.isHttpOnly());

        if (m_insertCookieStmt->step() != SQLResultDone)
            qDebug()<<"QPersistentCookieJar: Failed to insert cookie into db: "<<m_db->lastErrorMsg();
        cookiesAdded += sqlite3_changes(m_db->sqlite3Handle());
        m_insertCookieStmt->reset();
        m_insertCookieStmt->finalize();
    }
    return (cookiesAdded > 0);

}

// This function logic is copied from QNetworkCookieJar::cookiesForUrl() and modified to handle DB requests
QList<QNetworkCookie> QPersistentCookieJar::cookiesForUrl(const QUrl &url) const
{
    uint now = (QDateTime::currentDateTimeUtc()).toTime_t();
    QList<QNetworkCookie> result;
    bool isEncrypted = url.scheme().toLower() == QLatin1String("https");

    if (!m_enableCookies || !m_dbReady || !m_db || !m_selectCookieStmt || (m_selectCookieStmt->prepare() != SQLResultOk))
        return result;
    int sqlResult;

    while (true) {
        sqlResult = m_selectCookieStmt->step();
        if (sqlResult == SQLResultDone || sqlResult != SQLResultRow)
            break;

        QNetworkCookie matchingCookie;

        QString cookieDomain = m_selectCookieStmt->getColumnText(0);
        QString cookiePath = m_selectCookieStmt->getColumnText(1);
        uint cookieExpires = m_selectCookieStmt->getColumnInt64(4);
        int cookieIsSecure = m_selectCookieStmt->getColumnInt64(5);

        if ((!isParentDomain(url.host(), cookieDomain))
                || (!isParentPath(url.path(), cookiePath))
                || ( cookieExpires != 0 && cookieExpires < now)
                || (cookieIsSecure == 1 && !isEncrypted)
           )
            continue;

        matchingCookie.setName(m_selectCookieStmt->getColumnText(2).utf8().data());
        matchingCookie.setValue(m_selectCookieStmt->getColumnText(3).utf8().data());
        matchingCookie.setHttpOnly(m_selectCookieStmt->getColumnInt(6));

        matchingCookie.setDomain(cookieDomain);
        matchingCookie.setPath(cookiePath);
        if(cookieExpires != 0) {
            QDateTime dt;
            dt.setTime_t(cookieExpires);
            matchingCookie.setExpirationDate(dt);
        }
        matchingCookie.setSecure(cookieIsSecure);

        // insert this cookie into result, sorted by path
        QList<QNetworkCookie>::Iterator insertIt = result.begin();
        while (insertIt != result.end()) {
            if (insertIt->path().length() < matchingCookie.path().length()) {
                insertIt = result.insert(insertIt, matchingCookie);
                break;
            } else
                ++insertIt;
        }
        if (insertIt == result.end())
            result += matchingCookie;
    }
    m_selectCookieStmt->reset();
    m_selectCookieStmt->finalize();
    return result;
}

bool QPersistentCookieJar::deleteSessionAndExpiredCookies()
{
    if (m_dbReady && !m_db->executeCommand("DELETE FROM Cookies WHERE strftime(\'\%s\',\'now\') > expires"))
        return false;
    return true;
}

bool QPersistentCookieJar::clearCookies()
{
    if (m_dbReady && !m_db->executeCommand("DELETE FROM Cookies"))
        return false;
    return true;
}
