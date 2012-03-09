/*
    Copyright (C) 2011 Hewlett-Packard Development Company, L.P.

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

#ifndef QPersistentCookieJar_H
#define QPersistentCookieJar_H

#include <sqlite3.h>
#include <QTimer>
#include <QString>
#include <QtNetwork/QNetworkCookieJar>

#include "qwebkitglobal.h"

namespace WebCore {
class SQLiteDatabase;
class SQLiteStatement;
}
class QWEBKIT_EXPORT QPersistentCookieJar : public QNetworkCookieJar {
    Q_OBJECT

    public:
        QPersistentCookieJar(QObject* parent = 0, QString appId="");
        virtual ~QPersistentCookieJar();
        virtual bool setCookiesFromUrl(const QList<QNetworkCookie> &cookieList, const QUrl &url);
        virtual QList<QNetworkCookie> cookiesForUrl(const QUrl &url) const;
        void enableCookies(bool enableCookies) { m_enableCookies = enableCookies;}
        bool isCookiesEnabled() const { return m_enableCookies; }
        bool clearCookies();

    private:
        bool initDb();
        bool createTables();
        bool isValidDb();
        void closeDb();
        bool deleteSessionAndExpiredCookies();


        bool m_dbReady;
        bool m_enableCookies;
        QString m_appId;
        QString m_dbPath;
        WebCore::SQLiteDatabase*     m_db;
        WebCore::SQLiteStatement*    m_insertCookieStmt;
        WebCore::SQLiteStatement*    m_selectCookieStmt;
        WebCore::SQLiteStatement*    m_deleteCookieStmt;
        QList<QByteArray> m_rawCookies;

};
#endif
