/*
 * Copyright (C) 2011 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Hewlett-Packard Development Company, L.P.
 * nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef QPersistentCookieJar_H
#define QPersistentCookieJar_H

#include <sqlite3.h>
#include <QTimer>
#include <QString>
#include <QNetworkCookieJar>

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
