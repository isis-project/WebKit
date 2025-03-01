/*
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "qt_runtime.h"

#include "APICast.h"
#include "BooleanObject.h"
#include "DateInstance.h"
#include "DatePrototype.h"
#include "FunctionPrototype.h"
#include "Interpreter.h"
#include "JSArray.h"
#include "JSDocument.h"
#include "JSDOMBinding.h"
#include "JSDOMWindow.h"
#include <JSFunction.h>
#include "JSGlobalObject.h"
#include "JSHTMLElement.h"
#include "JSLock.h"
#include "JSObject.h"
#include "JSRetainPtr.h"
#include "JSUint8ClampedArray.h"
#include "ObjectPrototype.h"
#include "PropertyNameArray.h"
#include "RegExpConstructor.h"
#include "RegExpObject.h"
#include "qdatetime.h"
#include "qdebug.h"
#include "qmetaobject.h"
#include "qmetatype.h"
#include "qobject.h"
#include "qstringlist.h"
#include "qt_instance.h"
#include "qt_pixmapruntime.h"
#include "qvarlengtharray.h"

#include <wtf/DateMath.h>

#include <limits.h>
#include <runtime/Error.h>
#include <runtime_array.h>
#include <runtime_object.h>

// QtScript has these
Q_DECLARE_METATYPE(QObjectList);
Q_DECLARE_METATYPE(QList<int>);
Q_DECLARE_METATYPE(QVariant);

using namespace WebCore;

namespace JSC {
namespace Bindings {

// Debugging
//#define QTWK_RUNTIME_CONVERSION_DEBUG
//#define QTWK_RUNTIME_MATCH_DEBUG

class QWKNoDebug
{
public:
    inline QWKNoDebug(){}
    inline ~QWKNoDebug(){}

    template<typename T>
    inline QWKNoDebug &operator<<(const T &) { return *this; }
};

#ifdef QTWK_RUNTIME_CONVERSION_DEBUG
#define qConvDebug() qDebug()
#else
#define qConvDebug() QWKNoDebug()
#endif

#ifdef QTWK_RUNTIME_MATCH_DEBUG
#define qMatchDebug() qDebug()
#else
#define qMatchDebug() QWKNoDebug()
#endif

typedef enum {
    Variant = 0,
    Number,
    Boolean,
    String,
    Date,
    RegExp,
    Array,
    QObj,
    Object,
    Null,
    RTArray,
    JSUint8ClampedArray
} JSRealType;

#if defined(QTWK_RUNTIME_CONVERSION_DEBUG) || defined(QTWK_RUNTIME_MATCH_DEBUG)
QDebug operator<<(QDebug dbg, const JSRealType &c)
{
     const char *map[] = { "Variant", "Number", "Boolean", "String", "Date",
         "RegExp", "Array", "RTObject", "Object", "Null", "RTArray"};

     dbg.nospace() << "JSType(" << ((int)c) << ", " <<  map[c] << ")";

     return dbg.space();
}
#endif

struct RuntimeConversion {
    ConvertToJSValueFunction toJSValueFunc;
    ConvertToVariantFunction toVariantFunc;
};

typedef QHash<int, RuntimeConversion> RuntimeConversionTable;
Q_GLOBAL_STATIC(RuntimeConversionTable, customRuntimeConversions)

void registerCustomType(int qtMetaTypeId, ConvertToVariantFunction toVariantFunc, ConvertToJSValueFunction toJSValueFunc)
{
    RuntimeConversion conversion;
    conversion.toJSValueFunc = toJSValueFunc;
    conversion.toVariantFunc = toVariantFunc;
    customRuntimeConversions()->insert(qtMetaTypeId, conversion);
}

static bool isJSUint8ClampedArray(JSValue val)
{
    return val.isCell() && val.inherits(&JSUint8ClampedArray::s_info);
}

static JSRealType valueRealType(ExecState* exec, JSValue val)
{
    if (val.isNumber())
        return Number;
    else if (val.isString())
        return String;
    else if (val.isBoolean())
        return Boolean;
    else if (val.isNull())
        return Null;
    else if (isJSUint8ClampedArray(val))
        return JSUint8ClampedArray;
    else if (val.isObject()) {
        JSObject *object = val.toObject(exec);
        if (object->inherits(&RuntimeArray::s_info))  // RuntimeArray 'inherits' from Array, but not in C++
            return RTArray;
        else if (object->inherits(&JSArray::s_info))
            return Array;
        else if (object->inherits(&DateInstance::s_info))
            return Date;
        else if (object->inherits(&RegExpObject::s_info))
            return RegExp;
        else if (object->inherits(&RuntimeObject::s_info))
            return QObj;
        return Object;
    }

    return String; // I don't know.
}

QVariant convertValueToQVariant(ExecState*, JSValue, QMetaType::Type, int*, HashSet<JSObject*>*, int);

static QVariantMap convertValueToQVariantMap(ExecState* exec, JSObject* object, HashSet<JSObject*>* visitedObjects, int recursionLimit)
{
    Q_ASSERT(!exec->hadException());

    PropertyNameArray properties(exec);
    object->methodTable()->getPropertyNames(object, exec, properties, ExcludeDontEnumProperties);
    PropertyNameArray::const_iterator it = properties.begin();
    QVariantMap result;
    int objdist = 0;

    while (it != properties.end()) {
        if (object->propertyIsEnumerable(exec, *it)) {
            JSValue val = object->get(exec, *it);
            if (exec->hadException())
                exec->clearException();
            else {
                QVariant v = convertValueToQVariant(exec, val, QMetaType::Void, &objdist, visitedObjects, recursionLimit);
                if (objdist >= 0) {
                    UString ustring = (*it).ustring();
                    QString id = QString((const QChar*)ustring.impl()->characters(), ustring.length());
                    result.insert(id, v);
                }
            }
        }
        ++it;
    }
    return result;
}

QVariant convertValueToQVariant(ExecState* exec, JSValue value, QMetaType::Type hint, int *distance, HashSet<JSObject*>* visitedObjects, int recursionLimit)
{
    --recursionLimit;

    if (!value || !recursionLimit)
        return QVariant();

    JSObject* object = 0;
    if (value.isObject()) {
        object = value.toObject(exec);
        if (visitedObjects->contains(object))
            return QVariant();

        visitedObjects->add(object);
    }

    // check magic pointer values before dereferencing value
    if (value == jsNaN()
        || (value == jsUndefined()
            && hint != QMetaType::QString
            && hint != (QMetaType::Type) qMetaTypeId<QVariant>())) {
        if (distance)
            *distance = -1;
        return QVariant();
    }

    JSLock lock(SilenceAssertionsOnly);
    JSRealType type = valueRealType(exec, value);
    if (hint == QMetaType::Void) {
        switch(type) {
            case Number:
                hint = QMetaType::Double;
                break;
            case Boolean:
                hint = QMetaType::Bool;
                break;
            case String:
            default:
                hint = QMetaType::QString;
                break;
            case Date:
                hint = QMetaType::QDateTime;
                break;
            case RegExp:
                hint = QMetaType::QRegExp;
                break;
            case Object:
                if (object->inherits(&NumberObject::s_info))
                    hint = QMetaType::Double;
                else if (object->inherits(&BooleanObject::s_info))
                    hint = QMetaType::Bool;
                else
                    hint = QMetaType::QVariantMap;
                break;
            case QObj:
                hint = QMetaType::QObjectStar;
                break;
            case JSUint8ClampedArray:
                hint = QMetaType::QByteArray;
                break;
            case Array:
            case RTArray:
                hint = QMetaType::QVariantList;
                break;
        }
    }

    qConvDebug() << "convertValueToQVariant: jstype is " << type << ", hint is" << hint;

    if (value == jsNull()
        && hint != QMetaType::QObjectStar
        && hint != QMetaType::VoidStar
        && hint != QMetaType::QString
        && hint != (QMetaType::Type) qMetaTypeId<QVariant>()) {
        if (distance)
            *distance = -1;
        return QVariant();
    }

    QVariant ret;
    int dist = -1;
    switch (hint) {
        case QMetaType::Bool:
            if (type == Object && object->inherits(&BooleanObject::s_info))
                ret = QVariant(asBooleanObject(value)->internalValue().toBoolean(exec));
            else
                ret = QVariant(value.toBoolean(exec));
            if (type == Boolean)
                dist = 0;
            else
                dist = 10;
            break;

        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Float:
        case QMetaType::Double:
            ret = QVariant(value.toNumber(exec));
            ret.convert((QVariant::Type)hint);
            if (type == Number) {
                switch (hint) {
                case QMetaType::Double:
                    dist = 0;
                    break;
                case QMetaType::Float:
                    dist = 1;
                    break;
                case QMetaType::LongLong:
                case QMetaType::ULongLong:
                    dist = 2;
                    break;
                case QMetaType::Long:
                case QMetaType::ULong:
                    dist = 3;
                    break;
                case QMetaType::Int:
                case QMetaType::UInt:
                    dist = 4;
                    break;
                case QMetaType::Short:
                case QMetaType::UShort:
                    dist = 5;
                    break;
                    break;
                default:
                    dist = 10;
                    break;
                }
            } else {
                dist = 10;
            }
            break;

        case QMetaType::QChar:
            if (type == Number || type == Boolean) {
                ret = QVariant(QChar((ushort)value.toNumber(exec)));
                if (type == Boolean)
                    dist = 3;
                else
                    dist = 6;
            } else {
                UString str = value.toString(exec)->value(exec);
                ret = QVariant(QChar(str.length() ? *(const ushort*)str.impl()->characters() : 0));
                if (type == String)
                    dist = 3;
                else
                    dist = 10;
            }
            break;

        case QMetaType::QString: {
            if (value.isUndefinedOrNull()) {
                if (distance)
                    *distance = 1;
                return QString();
            } else {
                UString ustring = value.toString(exec)->value(exec);
                ret = QVariant(QString((const QChar*)ustring.impl()->characters(), ustring.length()));
                if (type == String)
                    dist = 0;
                else
                    dist = 10;
            }
            break;
        }

        case QMetaType::QVariantMap:
            if (type == Object || type == Array || type == RTArray) {
                ret = QVariant(convertValueToQVariantMap(exec, object, visitedObjects, recursionLimit));
                // Those types can still have perfect matches, e.g. 'bool' if value is a Boolean Object.
                dist = 1;
            }
            break;

        case QMetaType::QVariantList:
            if (type == RTArray) {
                RuntimeArray* rtarray = static_cast<RuntimeArray*>(object);

                QVariantList result;
                int len = rtarray->getLength();
                int objdist = 0;
                qConvDebug() << "converting a " << len << " length Array";
                for (int i = 0; i < len; ++i) {
                    JSValue val = rtarray->getConcreteArray()->valueAt(exec, i);
                    result.append(convertValueToQVariant(exec, val, QMetaType::Void, &objdist, visitedObjects, recursionLimit));
                    if (objdist == -1) {
                        qConvDebug() << "Failed converting element at index " << i;
                        break; // Failed converting a list entry, so fail the array
                    }
                }
                if (objdist != -1) {
                    dist = 5;
                    ret = QVariant(result);
                }
            } else if (type == Array) {
                JSArray* array = static_cast<JSArray*>(object);

                QVariantList result;
                int len = array->length();
                int objdist = 0;
                qConvDebug() << "converting a " << len << " length Array";
                for (int i = 0; i < len; ++i) {
                    JSValue val = array->get(exec, i);
                    result.append(convertValueToQVariant(exec, val, QMetaType::Void, &objdist, visitedObjects, recursionLimit));
                    if (objdist == -1) {
                        qConvDebug() << "Failed converting element at index " << i;
                        break; // Failed converting a list entry, so fail the array
                    }
                }
                if (objdist != -1) {
                    dist = 5;
                    ret = QVariant(result);
                }
            } else {
                // Make a single length array
                int objdist;
                qConvDebug() << "making a single length variantlist";
                QVariant var = convertValueToQVariant(exec, value, QMetaType::Void, &objdist, visitedObjects, recursionLimit);
                if (objdist != -1) {
                    QVariantList result;
                    result << var;
                    ret = QVariant(result);
                    dist = 10;
                } else {
                    qConvDebug() << "failed making single length varlist";
                }
            }
            break;

        case QMetaType::QStringList: {
            if (type == RTArray) {
                RuntimeArray* rtarray = static_cast<RuntimeArray*>(object);

                QStringList result;
                int len = rtarray->getLength();
                for (int i = 0; i < len; ++i) {
                    JSValue val = rtarray->getConcreteArray()->valueAt(exec, i);
                    UString ustring = val.toString(exec)->value(exec);
                    QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());

                    result.append(qstring);
                }
                dist = 5;
                ret = QVariant(result);
            } else if (type == Array) {
                JSArray* array = static_cast<JSArray*>(object);

                QStringList result;
                int len = array->length();
                for (int i = 0; i < len; ++i) {
                    JSValue val = array->get(exec, i);
                    UString ustring = val.toString(exec)->value(exec);
                    QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());

                    result.append(qstring);
                }
                dist = 5;
                ret = QVariant(result);
            } else {
                // Make a single length array
                UString ustring = value.toString(exec)->value(exec);
                QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());
                QStringList result;
                result.append(qstring);
                ret = QVariant(result);
                dist = 10;
            }
            break;
        }

        case QMetaType::QByteArray: {
            if (type == JSUint8ClampedArray) {
                WTF::Uint8ClampedArray* arr = toUint8ClampedArray(value);
                ret = QVariant(QByteArray(reinterpret_cast<const char*>(arr->data()), arr->length()));
                dist = 0;
            } else {
                UString ustring = value.toString(exec)->value(exec);
                ret = QVariant(QString((const QChar*)ustring.impl()->characters(), ustring.length()).toLatin1());
                if (type == String)
                    dist = 5;
                else
                    dist = 10;
            }
            break;
        }

        case QMetaType::QDateTime:
        case QMetaType::QDate:
        case QMetaType::QTime:
            if (type == Date) {
                DateInstance* date = static_cast<DateInstance*>(object);
                GregorianDateTime gdt;
                msToGregorianDateTime(exec, date->internalNumber(), true, gdt);
                if (hint == QMetaType::QDateTime) {
                    ret = QDateTime(QDate(gdt.year + 1900, gdt.month + 1, gdt.monthDay), QTime(gdt.hour, gdt.minute, gdt.second), Qt::UTC);
                    dist = 0;
                } else if (hint == QMetaType::QDate) {
                    ret = QDate(gdt.year + 1900, gdt.month + 1, gdt.monthDay);
                    dist = 1;
                } else {
                    ret = QTime(gdt.hour + 1900, gdt.minute, gdt.second);
                    dist = 2;
                }
            } else if (type == Number) {
                double b = value.toNumber(exec);
                GregorianDateTime gdt;
                msToGregorianDateTime(exec, b, true, gdt);
                if (hint == QMetaType::QDateTime) {
                    ret = QDateTime(QDate(gdt.year + 1900, gdt.month + 1, gdt.monthDay), QTime(gdt.hour, gdt.minute, gdt.second), Qt::UTC);
                    dist = 6;
                } else if (hint == QMetaType::QDate) {
                    ret = QDate(gdt.year + 1900, gdt.month + 1, gdt.monthDay);
                    dist = 8;
                } else {
                    ret = QTime(gdt.hour, gdt.minute, gdt.second);
                    dist = 10;
                }
#ifndef QT_NO_DATESTRING
            } else if (type == String) {
                UString ustring = value.toString(exec)->value(exec);
                QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());

                if (hint == QMetaType::QDateTime) {
                    QDateTime dt = QDateTime::fromString(qstring, Qt::ISODate);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(qstring, Qt::TextDate);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(qstring, Qt::SystemLocaleDate);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(qstring, Qt::LocaleDate);
                    if (dt.isValid()) {
                        ret = dt;
                        dist = 2;
                    }
                } else if (hint == QMetaType::QDate) {
                    QDate dt = QDate::fromString(qstring, Qt::ISODate);
                    if (!dt.isValid())
                        dt = QDate::fromString(qstring, Qt::TextDate);
                    if (!dt.isValid())
                        dt = QDate::fromString(qstring, Qt::SystemLocaleDate);
                    if (!dt.isValid())
                        dt = QDate::fromString(qstring, Qt::LocaleDate);
                    if (dt.isValid()) {
                        ret = dt;
                        dist = 3;
                    }
                } else {
                    QTime dt = QTime::fromString(qstring, Qt::ISODate);
                    if (!dt.isValid())
                        dt = QTime::fromString(qstring, Qt::TextDate);
                    if (!dt.isValid())
                        dt = QTime::fromString(qstring, Qt::SystemLocaleDate);
                    if (!dt.isValid())
                        dt = QTime::fromString(qstring, Qt::LocaleDate);
                    if (dt.isValid()) {
                        ret = dt;
                        dist = 3;
                    }
                }
#endif // QT_NO_DATESTRING
            }
            break;

        case QMetaType::QRegExp:
            if (type == RegExp) {
/*
                RegExpObject *re = static_cast<RegExpObject*>(object);
*/
                // Attempt to convert.. a bit risky
                UString ustring = value.toString(exec)->value(exec);
                QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());

                // this is of the form '/xxxxxx/i'
                int firstSlash = qstring.indexOf(QLatin1Char('/'));
                int lastSlash = qstring.lastIndexOf(QLatin1Char('/'));
                if (firstSlash >=0 && lastSlash > firstSlash) {
                    QRegExp realRe;

                    realRe.setPattern(qstring.mid(firstSlash + 1, lastSlash - firstSlash - 1));

                    if (qstring.mid(lastSlash + 1).contains(QLatin1Char('i')))
                        realRe.setCaseSensitivity(Qt::CaseInsensitive);

                    ret = QVariant::fromValue(realRe);
                    dist = 0;
                } else {
                    qConvDebug() << "couldn't parse a JS regexp";
                }
            } else if (type == String) {
                UString ustring = value.toString(exec)->value(exec);
                QString qstring = QString((const QChar*)ustring.impl()->characters(), ustring.length());

                QRegExp re(qstring);
                if (re.isValid()) {
                    ret = QVariant::fromValue(re);
                    dist = 10;
                }
            }
            break;

        case QMetaType::QObjectStar:
            if (type == QObj) {
                QtInstance* qtinst = QtInstance::getInstance(object);
                if (qtinst) {
                    if (qtinst->getObject()) {
                        qConvDebug() << "found instance, with object:" << (void*) qtinst->getObject();
                        ret = QVariant::fromValue(qtinst->getObject());
                        qConvDebug() << ret;
                        dist = 0;
                    } else {
                        qConvDebug() << "can't convert deleted qobject";
                    }
                } else {
                    qConvDebug() << "wasn't a qtinstance";
                }
            } else if (type == Null) {
                QObject* nullobj = 0;
                ret = QVariant::fromValue(nullobj);
                dist = 0;
            } else {
                qConvDebug() << "previous type was not an object:" << type;
            }
            break;

        case QMetaType::VoidStar:
            if (type == QObj) {
                QtInstance* qtinst = QtInstance::getInstance(object);
                if (qtinst) {
                    if (qtinst->getObject()) {
                        qConvDebug() << "found instance, with object:" << (void*) qtinst->getObject();
                        ret = QVariant::fromValue((void *)qtinst->getObject());
                        qConvDebug() << ret;
                        dist = 0;
                    } else {
                        qConvDebug() << "can't convert deleted qobject";
                    }
                } else {
                    qConvDebug() << "wasn't a qtinstance";
                }
            } else if (type == Null) {
                ret = QVariant::fromValue((void*)0);
                dist = 0;
            } else if (type == Number) {
                // I don't think that converting a double to a pointer is a wise
                // move.  Except maybe 0.
                qConvDebug() << "got number for void * - not converting, seems unsafe:" << value.toNumber(exec);
            } else {
                qConvDebug() << "void* - unhandled type" << type;
            }
            break;

        default:
            // Non const type ids
            if (hint == (QMetaType::Type) qMetaTypeId<QObjectList>())
            {
                if (type == RTArray) {
                    RuntimeArray* rtarray = static_cast<RuntimeArray*>(object);

                    QObjectList result;
                    int len = rtarray->getLength();
                    for (int i = 0; i < len; ++i) {
                        JSValue val = rtarray->getConcreteArray()->valueAt(exec, i);
                        int itemdist = -1;
                        QVariant item = convertValueToQVariant(exec, val, QMetaType::QObjectStar, &itemdist, visitedObjects, recursionLimit);
                        if (itemdist >= 0)
                            result.append(item.value<QObject*>());
                        else
                            break;
                    }
                    // If we didn't fail conversion
                    if (result.count() == len) {
                        dist = 5;
                        ret = QVariant::fromValue(result);
                    }
                } else if (type == Array) {
                    JSObject* object = value.toObject(exec);
                    JSArray* array = static_cast<JSArray *>(object);
                    QObjectList result;
                    int len = array->length();
                    for (int i = 0; i < len; ++i) {
                        JSValue val = array->get(exec, i);
                        int itemdist = -1;
                        QVariant item = convertValueToQVariant(exec, val, QMetaType::QObjectStar, &itemdist, visitedObjects, recursionLimit);
                        if (itemdist >= 0)
                            result.append(item.value<QObject*>());
                        else
                            break;
                    }
                    // If we didn't fail conversion
                    if (result.count() == len) {
                        dist = 5;
                        ret = QVariant::fromValue(result);
                    }
                } else {
                    // Make a single length array
                    QObjectList result;
                    int itemdist = -1;
                    QVariant item = convertValueToQVariant(exec, value, QMetaType::QObjectStar, &itemdist, visitedObjects, recursionLimit);
                    if (itemdist >= 0) {
                        result.append(item.value<QObject*>());
                        dist = 10;
                        ret = QVariant::fromValue(result);
                    }
                }
                break;
            } else if (hint == (QMetaType::Type) qMetaTypeId<QList<int> >()) {
                if (type == RTArray) {
                    RuntimeArray* rtarray = static_cast<RuntimeArray*>(object);

                    QList<int> result;
                    int len = rtarray->getLength();
                    for (int i = 0; i < len; ++i) {
                        JSValue val = rtarray->getConcreteArray()->valueAt(exec, i);
                        int itemdist = -1;
                        QVariant item = convertValueToQVariant(exec, val, QMetaType::Int, &itemdist, visitedObjects, recursionLimit);
                        if (itemdist >= 0)
                            result.append(item.value<int>());
                        else
                            break;
                    }
                    // If we didn't fail conversion
                    if (result.count() == len) {
                        dist = 5;
                        ret = QVariant::fromValue(result);
                    }
                } else if (type == Array) {
                    JSArray* array = static_cast<JSArray *>(object);

                    QList<int> result;
                    int len = array->length();
                    for (int i = 0; i < len; ++i) {
                        JSValue val = array->get(exec, i);
                        int itemdist = -1;
                        QVariant item = convertValueToQVariant(exec, val, QMetaType::Int, &itemdist, visitedObjects, recursionLimit);
                        if (itemdist >= 0)
                            result.append(item.value<int>());
                        else
                            break;
                    }
                    // If we didn't fail conversion
                    if (result.count() == len) {
                        dist = 5;
                        ret = QVariant::fromValue(result);
                    }
                } else {
                    // Make a single length array
                    QList<int> result;
                    int itemdist = -1;
                    QVariant item = convertValueToQVariant(exec, value, QMetaType::Int, &itemdist, visitedObjects, recursionLimit);
                    if (itemdist >= 0) {
                        result.append(item.value<int>());
                        dist = 10;
                        ret = QVariant::fromValue(result);
                    }
                }
                break;
            } else if (QtPixmapInstance::canHandle(static_cast<QMetaType::Type>(hint))) {
                ret = QtPixmapInstance::variantFromObject(object, static_cast<QMetaType::Type>(hint));
            } else if (customRuntimeConversions()->contains(hint)) {
                ret = customRuntimeConversions()->value(hint).toVariantFunc(object, &dist, visitedObjects);
                if (dist == 0)
                    break;
            } else if (hint == (QMetaType::Type) qMetaTypeId<QVariant>()) {
                if (value.isUndefinedOrNull()) {
                    if (distance)
                        *distance = 1;
                    return QVariant();
                } else {
                    if (type == Object) {
                        // Since we haven't really visited this object yet, we remove it
                        visitedObjects->remove(object);
                    }

                    // And then recurse with the autodetect flag
                    ret = convertValueToQVariant(exec, value, QMetaType::Void, distance, visitedObjects, recursionLimit);
                    dist = 10;
                }
                break;
            }

            dist = 10;
            break;
    }

    if (!ret.isValid())
        dist = -1;
    if (distance)
        *distance = dist;

    return ret;
}

QVariant convertValueToQVariant(ExecState* exec, JSValue value, QMetaType::Type hint, int *distance)
{
    const int recursionLimit = 200;
    HashSet<JSObject*> visitedObjects;
    return convertValueToQVariant(exec, value, hint, distance, &visitedObjects, recursionLimit);
}

JSValue convertQVariantToValue(ExecState* exec, PassRefPtr<RootObject> root, const QVariant& variant)
{
    // Variants with QObject * can be isNull but not a null pointer
    // An empty QString variant is also null
    QMetaType::Type type = (QMetaType::Type) variant.userType();

    qConvDebug() << "convertQVariantToValue: metatype:" << type << ", isnull: " << variant.isNull();
    if (variant.isNull() &&
        type != QMetaType::QObjectStar &&
        type != QMetaType::VoidStar &&
        type != QMetaType::QWidgetStar &&
        type != QMetaType::QString) {
        return jsNull();
    }

    JSLock lock(SilenceAssertionsOnly);

    if (type == QMetaType::Bool)
        return jsBoolean(variant.toBool());

    if (type == QMetaType::Int ||
        type == QMetaType::UInt ||
        type == QMetaType::Long ||
        type == QMetaType::ULong ||
        type == QMetaType::LongLong ||
        type == QMetaType::ULongLong ||
        type == QMetaType::Short ||
        type == QMetaType::UShort ||
        type == QMetaType::Float ||
        type == QMetaType::Double)
        return jsNumber(variant.toDouble());

    if (type == QMetaType::QRegExp) {
        QRegExp re = variant.value<QRegExp>();

        if (re.isValid()) {
            UString pattern((UChar*)re.pattern().utf16(), re.pattern().length());
            RegExpFlags flags = (re.caseSensitivity() == Qt::CaseInsensitive) ? FlagIgnoreCase : NoFlags;

            JSC::RegExp* regExp = JSC::RegExp::create(exec->globalData(), pattern, flags);
            if (regExp->isValid())
                return RegExpObject::create(exec, exec->lexicalGlobalObject(), exec->lexicalGlobalObject()->regExpStructure(), regExp);
            return jsNull();
        }
    }

    if (type == QMetaType::QDateTime ||
        type == QMetaType::QDate ||
        type == QMetaType::QTime) {

        QDate date = QDate::currentDate();
        QTime time(0,0,0); // midnight

        if (type == QMetaType::QDate)
            date = variant.value<QDate>();
        else if (type == QMetaType::QTime)
            time = variant.value<QTime>();
        else {
            QDateTime dt = variant.value<QDateTime>().toLocalTime();
            date = dt.date();
            time = dt.time();
        }

        // Dates specified this way are in local time (we convert DateTimes above)
        GregorianDateTime dt;
        dt.year = date.year() - 1900;
        dt.month = date.month() - 1;
        dt.monthDay = date.day();
        dt.hour = time.hour();
        dt.minute = time.minute();
        dt.second = time.second();
        dt.isDST = -1;
        double ms = gregorianDateTimeToMS(exec, dt, time.msec(), /*inputIsUTC*/ false);

        return DateInstance::create(exec, exec->lexicalGlobalObject()->dateStructure(), trunc(ms));
    }

    if (type == QMetaType::QByteArray) {
        QByteArray qtByteArray = variant.value<QByteArray>();
        WTF::RefPtr<WTF::Uint8ClampedArray> wtfByteArray = WTF::Uint8ClampedArray::createUninitialized(qtByteArray.length());
        memcpy(wtfByteArray->data(), qtByteArray.constData(), qtByteArray.length());
        return toJS(exec, static_cast<JSDOMGlobalObject*>(exec->lexicalGlobalObject()), wtfByteArray.get());
    }

    if (type == QMetaType::QObjectStar || type == QMetaType::QWidgetStar) {
        QObject* obj = variant.value<QObject*>();
        if (!obj)
            return jsNull();
        return QtInstance::getQtInstance(obj, root, QScriptEngine::QtOwnership)->createRuntimeObject(exec);
    }

    if (QtPixmapInstance::canHandle(static_cast<QMetaType::Type>(variant.type())))
        return QtPixmapInstance::createPixmapRuntimeObject(exec, root, variant);

    if (customRuntimeConversions()->contains(type)) {
        if (!root->globalObject()->inherits(&JSDOMWindow::s_info))
            return jsUndefined();

        Document* document = (static_cast<JSDOMWindow*>(root->globalObject()))->impl()->document();
        if (!document)
            return jsUndefined();
        return customRuntimeConversions()->value(type).toJSValueFunc(exec, toJSDOMGlobalObject(document, exec), variant);
    }

    if (type == QMetaType::QVariantMap) {
        // create a new object, and stuff properties into it
        JSObject* ret = constructEmptyObject(exec);
        QVariantMap map = variant.value<QVariantMap>();
        QVariantMap::const_iterator i = map.constBegin();
        while (i != map.constEnd()) {
            QString s = i.key();
            JSValue val = convertQVariantToValue(exec, root.get(), i.value());
            if (val) {
                PutPropertySlot slot;
                ret->methodTable()->put(ret, exec, Identifier(&exec->globalData(), reinterpret_cast_ptr<const UChar *>(s.constData()), s.length()), val, slot);
                // ### error case?
            }
            ++i;
        }

        return ret;
    }

    // List types
    if (type == QMetaType::QVariantList) {
        QVariantList vl = variant.toList();
        qConvDebug() << "got a " << vl.count() << " length list:" << vl;
        return RuntimeArray::create(exec, new QtArray<QVariant>(vl, QMetaType::Void, root));
    } else if (type == QMetaType::QStringList) {
        QStringList sl = variant.value<QStringList>();
        return RuntimeArray::create(exec, new QtArray<QString>(sl, QMetaType::QString, root));
    } else if (type == (QMetaType::Type) qMetaTypeId<QObjectList>()) {
        QObjectList ol= variant.value<QObjectList>();
        return RuntimeArray::create(exec, new QtArray<QObject*>(ol, QMetaType::QObjectStar, root));
    } else if (type == (QMetaType::Type)qMetaTypeId<QList<int> >()) {
        QList<int> il= variant.value<QList<int> >();
        return RuntimeArray::create(exec, new QtArray<int>(il, QMetaType::Int, root));
    }

    if (type == (QMetaType::Type)qMetaTypeId<QVariant>()) {
        QVariant real = variant.value<QVariant>();
        qConvDebug() << "real variant is:" << real;
        return convertQVariantToValue(exec, root, real);
    }

    qConvDebug() << "fallback path for" << variant << variant.userType();

    QString string = variant.toString();
    UString ustring((UChar*)string.utf16(), string.length());
    return jsString(exec, ustring);
}

// ===============

// Qt-like macros
#define QW_D(Class) Class##Data* d = d_func()
#define QW_DS(Class,Instance) Class##Data* d = Instance->d_func()

const ClassInfo QtRuntimeMethod::s_info = { "QtRuntimeMethod", &InternalFunction::s_info, 0, 0, CREATE_METHOD_TABLE(QtRuntimeMethod) };

QtRuntimeMethod::QtRuntimeMethod(QtRuntimeMethodData* dd, ExecState* exec, Structure* structure, const UString& identifier)
    : InternalFunction(exec->lexicalGlobalObject(), structure)
    , d_ptr(dd)
{
}

void QtRuntimeMethod::finishCreation(ExecState* exec, const UString& identifier, PassRefPtr<QtInstance> instance)
{
    Base::finishCreation(exec->globalData(), identifier);
    QW_D(QtRuntimeMethod);
    d->m_instance = instance;
    d->m_finalizer = PassWeak<QtRuntimeMethod>(this, d);
}

QtRuntimeMethod::~QtRuntimeMethod()
{
    delete d_ptr;
}

void QtRuntimeMethod::destroy(JSCell* cell)
{
    jsCast<QtRuntimeMethod*>(cell)->QtRuntimeMethod::~QtRuntimeMethod();
}

// ===============

QtRuntimeMethodData::~QtRuntimeMethodData()
{
}

void QtRuntimeMethodData::finalize(Handle<Unknown> value, void*)
{
    m_instance->removeCachedMethod(static_cast<JSObject*>(value.get().asCell()));
}

QtRuntimeMetaMethodData::~QtRuntimeMetaMethodData()
{

}

QtRuntimeConnectionMethodData::~QtRuntimeConnectionMethodData()
{

}

// ===============

// Type conversion metadata (from QtScript originally)
class QtMethodMatchType
{
public:
    enum Kind {
        Invalid,
        Variant,
        MetaType,
        Unresolved,
        MetaEnum
    };


    QtMethodMatchType()
        : m_kind(Invalid) { }

    Kind kind() const
    { return m_kind; }

    QMetaType::Type typeId() const;

    bool isValid() const
    { return (m_kind != Invalid); }

    bool isVariant() const
    { return (m_kind == Variant); }

    bool isMetaType() const
    { return (m_kind == MetaType); }

    bool isUnresolved() const
    { return (m_kind == Unresolved); }

    bool isMetaEnum() const
    { return (m_kind == MetaEnum); }

    QByteArray name() const;

    int enumeratorIndex() const
    { Q_ASSERT(isMetaEnum()); return m_typeId; }

    static QtMethodMatchType variant()
    { return QtMethodMatchType(Variant); }

    static QtMethodMatchType metaType(int typeId, const QByteArray &name)
    { return QtMethodMatchType(MetaType, typeId, name); }

    static QtMethodMatchType metaEnum(int enumIndex, const QByteArray &name)
    { return QtMethodMatchType(MetaEnum, enumIndex, name); }

    static QtMethodMatchType unresolved(const QByteArray &name)
    { return QtMethodMatchType(Unresolved, /*typeId=*/0, name); }

private:
    QtMethodMatchType(Kind kind, int typeId = 0, const QByteArray &name = QByteArray())
        : m_kind(kind), m_typeId(typeId), m_name(name) { }

    Kind m_kind;
    int m_typeId;
    QByteArray m_name;
};

QMetaType::Type QtMethodMatchType::typeId() const
{
    if (isVariant())
        return (QMetaType::Type) QMetaType::type("QVariant");
    return (QMetaType::Type) (isMetaEnum() ? QMetaType::Int : m_typeId);
}

QByteArray QtMethodMatchType::name() const
{
    if (!m_name.isEmpty())
        return m_name;
    else if (m_kind == Variant)
        return "QVariant";
    return QByteArray();
}

struct QtMethodMatchData
{
    int matchDistance;
    int index;
    QVector<QtMethodMatchType> types;
    QVarLengthArray<QVariant, 10> args;

    QtMethodMatchData(int dist, int idx, QVector<QtMethodMatchType> typs,
                                const QVarLengthArray<QVariant, 10> &as)
        : matchDistance(dist), index(idx), types(typs), args(as) { }
    QtMethodMatchData()
        : index(-1) { }

    bool isValid() const
    { return (index != -1); }

    int firstUnresolvedIndex() const
    {
        for (int i=0; i < types.count(); i++) {
            if (types.at(i).isUnresolved())
                return i;
        }
        return -1;
    }
};

static int indexOfMetaEnum(const QMetaObject *meta, const QByteArray &str)
{
    QByteArray scope;
    QByteArray name;
    int scopeIdx = str.indexOf("::");
    if (scopeIdx != -1) {
        scope = str.left(scopeIdx);
        name = str.mid(scopeIdx + 2);
    } else {
        name = str;
    }
    for (int i = meta->enumeratorCount() - 1; i >= 0; --i) {
        QMetaEnum m = meta->enumerator(i);
        if ((m.name() == name)/* && (scope.isEmpty() || (m.scope() == scope))*/)
            return i;
    }
    return -1;
}

// Helper function for resolving methods
// Largely based on code in QtScript for compatibility reasons
static int findMethodIndex(ExecState* exec,
                           const QMetaObject* meta,
                           const QByteArray& signature,
                           bool allowPrivate,
                           QVarLengthArray<QVariant, 10> &vars,
                           void** vvars,
                           JSObject **pError)
{
    QList<int> matchingIndices;

    bool overloads = !signature.contains('(');

    int count = meta->methodCount();
    for (int i = count - 1; i >= 0; --i) {
        const QMetaMethod m = meta->method(i);

        // Don't choose private methods
        if (m.access() == QMetaMethod::Private && !allowPrivate)
            continue;

        // try and find all matching named methods
        if (!overloads && m.methodSignature() == signature)
            matchingIndices.append(i);
        else if (overloads && m.name() == signature)
            matchingIndices.append(i);
    }

    int chosenIndex = -1;
    *pError = 0;
    QVector<QtMethodMatchType> chosenTypes;

    QVarLengthArray<QVariant, 10> args;
    QVector<QtMethodMatchData> candidates;
    QVector<QtMethodMatchData> unresolved;
    QVector<int> tooFewArgs;
    QVector<int> conversionFailed;

    foreach(int index, matchingIndices) {
        QMetaMethod method = meta->method(index);

        QVector<QtMethodMatchType> types;
        bool unresolvedTypes = false;

        // resolve return type
        QByteArray returnTypeName = method.typeName();
        int rtype = method.returnType();
        if (rtype == QMetaType::UnknownType) {
            if (returnTypeName.endsWith('*')) {
                types.append(QtMethodMatchType::metaType(QMetaType::VoidStar, returnTypeName));
            } else {
                int enumIndex = indexOfMetaEnum(meta, returnTypeName);
                if (enumIndex != -1)
                    types.append(QtMethodMatchType::metaEnum(enumIndex, returnTypeName));
                else {
                    unresolvedTypes = true;
                    types.append(QtMethodMatchType::unresolved(returnTypeName));
                }
            }
        } else {
            if (rtype == QMetaType::QVariant)
                types.append(QtMethodMatchType::variant());
            else
                types.append(QtMethodMatchType::metaType(rtype, returnTypeName));
        }

        // resolve argument types
        QList<QByteArray> parameterTypeNames = method.parameterTypes();
        for (int i = 0; i < parameterTypeNames.count(); ++i) {
            QByteArray argTypeName = parameterTypeNames.at(i);
            int atype = method.parameterType(i);
            if (atype == QMetaType::UnknownType) {
                int enumIndex = indexOfMetaEnum(meta, argTypeName);
                if (enumIndex != -1)
                    types.append(QtMethodMatchType::metaEnum(enumIndex, argTypeName));
                else {
                    unresolvedTypes = true;
                    types.append(QtMethodMatchType::unresolved(argTypeName));
                }
            } else {
                if (atype == QMetaType::QVariant)
                    types.append(QtMethodMatchType::variant());
                else
                    types.append(QtMethodMatchType::metaType(atype, argTypeName));
            }
        }

        // If the native method requires more arguments than what was passed from JavaScript
        if (exec->argumentCount() + 1 < static_cast<unsigned>(types.count())) {
            qMatchDebug() << "Match:too few args for" << method.methodSignature();
            tooFewArgs.append(index);
            continue;
        }

        if (unresolvedTypes) {
            qMatchDebug() << "Match:unresolved arg types for" << method.methodSignature();
            // remember it so we can give an error message later, if necessary
            unresolved.append(QtMethodMatchData(/*matchDistance=*/INT_MAX, index,
                                                   types, QVarLengthArray<QVariant, 10>()));
            continue;
        }

        // Now convert arguments
        if (args.count() != types.count())
            args.resize(types.count());

        QtMethodMatchType retType = types[0];
        if (retType.typeId() != QMetaType::Void)
            args[0] = QVariant(retType.typeId(), (void *)0); // the return value

        bool converted = true;
        int matchDistance = 0;
        for (unsigned i = 0; converted && i + 1 < static_cast<unsigned>(types.count()); ++i) {
            JSValue arg = i < exec->argumentCount() ? exec->argument(i) : jsUndefined();

            int argdistance = -1;
            QVariant v = convertValueToQVariant(exec, arg, types.at(i+1).typeId(), &argdistance);
            if (argdistance >= 0) {
                matchDistance += argdistance;
                args[i+1] = v;
            } else {
                qMatchDebug() << "failed to convert argument " << i << "type" << types.at(i+1).typeId() << QMetaType::typeName(types.at(i+1).typeId());
                converted = false;
            }
        }

        qMatchDebug() << "Match: " << method.methodSignature() << (converted ? "converted":"failed to convert") << "distance " << matchDistance;

        if (converted) {
            if ((exec->argumentCount() + 1 == static_cast<unsigned>(types.count()))
                && (matchDistance == 0)) {
                // perfect match, use this one
                chosenIndex = index;
                break;
            } else {
                QtMethodMatchData currentMatch(matchDistance, index, types, args);
                if (candidates.isEmpty()) {
                    candidates.append(currentMatch);
                } else {
                    QtMethodMatchData bestMatchSoFar = candidates.at(0);
                    if ((args.count() > bestMatchSoFar.args.count())
                        || ((args.count() == bestMatchSoFar.args.count())
                            && (matchDistance <= bestMatchSoFar.matchDistance))) {
                        candidates.prepend(currentMatch);
                    } else {
                        candidates.append(currentMatch);
                    }
                }
            }
        } else {
            conversionFailed.append(index);
        }

        if (!overloads)
            break;
    }

    if (chosenIndex == -1 && candidates.count() == 0) {
        // No valid functions at all - format an error message
        if (!conversionFailed.isEmpty()) {
            QString message = QString::fromLatin1("incompatible type of argument(s) in call to %0(); candidates were\n")
                              .arg(QString::fromLatin1(signature));
            for (int i = 0; i < conversionFailed.size(); ++i) {
                if (i > 0)
                    message += QLatin1String("\n");
                QMetaMethod mtd = meta->method(conversionFailed.at(i));
                message += QString::fromLatin1("    %0").arg(QString::fromLatin1(mtd.methodSignature()));
            }
            *pError = throwError(exec, createTypeError(exec, message.toLatin1().constData()));
        } else if (!unresolved.isEmpty()) {
            QtMethodMatchData argsInstance = unresolved.first();
            int unresolvedIndex = argsInstance.firstUnresolvedIndex();
            Q_ASSERT(unresolvedIndex != -1);
            QtMethodMatchType unresolvedType = argsInstance.types.at(unresolvedIndex);
            QString message = QString::fromLatin1("cannot call %0(): unknown type `%1'")
                .arg(QString::fromLatin1(signature))
                .arg(QLatin1String(unresolvedType.name()));
            *pError = throwError(exec, createTypeError(exec, message.toLatin1().constData()));
        } else {
            QString message = QString::fromLatin1("too few arguments in call to %0(); candidates are\n")
                              .arg(QString::fromLatin1(signature));
            for (int i = 0; i < tooFewArgs.size(); ++i) {
                if (i > 0)
                    message += QLatin1String("\n");
                QMetaMethod mtd = meta->method(tooFewArgs.at(i));
                message += QString::fromLatin1("    %0").arg(QString::fromLatin1(mtd.methodSignature()));
            }
            *pError = throwError(exec, createSyntaxError(exec, message.toLatin1().constData()));
        }
    }

    if (chosenIndex == -1 && candidates.count() > 0) {
        QtMethodMatchData bestMatch = candidates.at(0);
        if ((candidates.size() > 1)
            && (bestMatch.args.count() == candidates.at(1).args.count())
            && (bestMatch.matchDistance == candidates.at(1).matchDistance)) {
            // ambiguous call
            QString message = QString::fromLatin1("ambiguous call of overloaded function %0(); candidates were\n")
                                .arg(QLatin1String(signature));
            for (int i = 0; i < candidates.size(); ++i) {
                // Only candidate for overload if argument count and match distance is same as best match
                if (candidates.at(i).args.count() == bestMatch.args.count()
                    || candidates.at(i).matchDistance == bestMatch.matchDistance) {
                    if (i > 0)
                        message += QLatin1String("\n");
                    QMetaMethod mtd = meta->method(candidates.at(i).index);
                    message += QString::fromLatin1("    %0").arg(QString::fromLatin1(mtd.methodSignature()));
                }
            }
            *pError = throwError(exec, createTypeError(exec, message.toLatin1().constData()));
        } else {
            chosenIndex = bestMatch.index;
            args = bestMatch.args;
        }
    }

    if (chosenIndex != -1) {
        /* Copy the stuff over */
        int i;
        vars.resize(args.count());
        for (i=0; i < args.count(); i++) {
            vars[i] = args[i];
            vvars[i] = vars[i].data();
        }
    }

    return chosenIndex;
}

// Signals are not fuzzy matched as much as methods
static int findSignalIndex(const QMetaObject* meta, int initialIndex, QByteArray signature)
{
    int index = initialIndex;
    QMetaMethod method = meta->method(index);
    bool overloads = !signature.contains('(');
    if (overloads && (method.attributes() & QMetaMethod::Cloned)) {
        // find the most general method
        do {
            method = meta->method(--index);
        } while (method.attributes() & QMetaMethod::Cloned);
    }
    return index;
}

const ClassInfo QtRuntimeMetaMethod::s_info = { "QtRuntimeMethod", &Base::s_info, 0, 0, CREATE_METHOD_TABLE(QtRuntimeMetaMethod) };

QtRuntimeMetaMethod::QtRuntimeMetaMethod(ExecState* exec, Structure* structure, const UString& identifier)
    : QtRuntimeMethod (new QtRuntimeMetaMethodData(), exec, structure, identifier)
{
}

void QtRuntimeMetaMethod::finishCreation(ExecState* exec, const UString& identifier, PassRefPtr<QtInstance> instance, int index, const QByteArray& signature, bool allowPrivate)
{
    Base::finishCreation(exec, identifier, instance);
    QW_D(QtRuntimeMetaMethod);
    d->m_signature = signature;
    d->m_index = index;
    d->m_allowPrivate = allowPrivate;
}

void QtRuntimeMetaMethod::visitChildren(JSCell* cell, SlotVisitor& visitor)
{
    QtRuntimeMetaMethod* thisObject = jsCast<QtRuntimeMetaMethod*>(cell);
    QtRuntimeMethod::visitChildren(thisObject, visitor);
    QtRuntimeMetaMethodData* d = thisObject->d_func();
    if (d->m_connect)
        visitor.append(&d->m_connect);
    if (d->m_disconnect)
        visitor.append(&d->m_disconnect);
}

EncodedJSValue QtRuntimeMetaMethod::call(ExecState* exec)
{
    QtRuntimeMetaMethodData* d = static_cast<QtRuntimeMetaMethod *>(exec->callee())->d_func();

    // We're limited to 10 args
    if (exec->argumentCount() > 10)
        return JSValue::encode(jsUndefined());

    // We have to pick a method that matches..
    JSLock lock(SilenceAssertionsOnly);

    QObject *obj = d->m_instance->getObject();
    if (obj) {
        QVarLengthArray<QVariant, 10> vargs;
        void *qargs[11];

        int methodIndex;
        JSObject* errorObj = 0;
        if ((methodIndex = findMethodIndex(exec, obj->metaObject(), d->m_signature, d->m_allowPrivate, vargs, (void **)qargs, &errorObj)) != -1) {
            if (QMetaObject::metacall(obj, QMetaObject::InvokeMetaMethod, methodIndex, qargs) >= 0)
                return JSValue::encode(jsUndefined());

            if (vargs[0].isValid())
                return JSValue::encode(convertQVariantToValue(exec, d->m_instance->rootObject(), vargs[0]));
        }

        if (errorObj)
            return JSValue::encode(errorObj);
    } else {
        return throwVMError(exec, createError(exec, "cannot call function of deleted QObject"));
    }

    // void functions return undefined
    return JSValue::encode(jsUndefined());
}

CallType QtRuntimeMetaMethod::getCallData(JSCell*, CallData& callData)
{
    callData.native.function = call;
    return CallTypeHost;
}

bool QtRuntimeMetaMethod::getOwnPropertySlot(JSCell* cell, ExecState* exec, PropertyName propertyName, PropertySlot& slot)
{
    QtRuntimeMetaMethod* thisObject = jsCast<QtRuntimeMetaMethod*>(cell);
    if (propertyName == Identifier(exec, "connect")) {
        slot.setCustom(thisObject, thisObject->connectGetter);
        return true;
    }
    if (propertyName == Identifier(exec, "disconnect")) {
        slot.setCustom(thisObject, thisObject->disconnectGetter);
        return true;
    }
    if (propertyName == exec->propertyNames().length) {
        slot.setCustom(thisObject, thisObject->lengthGetter);
        return true;
    }

    return QtRuntimeMethod::getOwnPropertySlot(thisObject, exec, propertyName, slot);
}

bool QtRuntimeMetaMethod::getOwnPropertyDescriptor(JSObject* object, ExecState* exec, PropertyName propertyName, PropertyDescriptor& descriptor)
{
    QtRuntimeMetaMethod* thisObject = jsCast<QtRuntimeMetaMethod*>(object);
    if (propertyName == Identifier(exec, "connect")) {
        PropertySlot slot;
        slot.setCustom(thisObject, connectGetter);
        descriptor.setDescriptor(slot.getValue(exec, propertyName), DontDelete | ReadOnly | DontEnum);
        return true;
    }

    if (propertyName == Identifier(exec, "disconnect")) {
        PropertySlot slot;
        slot.setCustom(thisObject, disconnectGetter);
        descriptor.setDescriptor(slot.getValue(exec, propertyName), DontDelete | ReadOnly | DontEnum);
        return true;
    }

    if (propertyName == exec->propertyNames().length) {
        PropertySlot slot;
        slot.setCustom(thisObject, lengthGetter);
        descriptor.setDescriptor(slot.getValue(exec, propertyName), DontDelete | ReadOnly | DontEnum);
        return true;
    }

    return QtRuntimeMethod::getOwnPropertyDescriptor(thisObject, exec, propertyName, descriptor);
}

void QtRuntimeMetaMethod::getOwnPropertyNames(JSObject* object, ExecState* exec, PropertyNameArray& propertyNames, EnumerationMode mode)
{
    if (mode == IncludeDontEnumProperties) {
        propertyNames.add(Identifier(exec, "connect"));
        propertyNames.add(Identifier(exec, "disconnect"));
        propertyNames.add(exec->propertyNames().length);
    }

    QtRuntimeMethod::getOwnPropertyNames(object, exec, propertyNames, mode);
}

JSValue QtRuntimeMetaMethod::lengthGetter(ExecState*, JSValue, PropertyName)
{
    // QtScript always returns 0
    return jsNumber(0);
}

JSValue QtRuntimeMetaMethod::connectGetter(ExecState* exec, JSValue slotBase, PropertyName ident)
{
    QtRuntimeMetaMethod* thisObj = static_cast<QtRuntimeMetaMethod*>(asObject(slotBase));
    QW_DS(QtRuntimeMetaMethod, thisObj);

    if (!d->m_connect)
        d->m_connect.set(exec->globalData(), thisObj, QtRuntimeConnectionMethod::create(exec, ident.impl(), true, d->m_instance, d->m_index, d->m_signature));
    return d->m_connect.get();
}

JSValue QtRuntimeMetaMethod::disconnectGetter(ExecState* exec, JSValue slotBase, PropertyName ident)
{
    QtRuntimeMetaMethod* thisObj = static_cast<QtRuntimeMetaMethod*>(asObject(slotBase));
    QW_DS(QtRuntimeMetaMethod, thisObj);

    if (!d->m_disconnect)
        d->m_disconnect.set(exec->globalData(), thisObj, QtRuntimeConnectionMethod::create(exec, ident.impl(), false, d->m_instance, d->m_index, d->m_signature));
    return d->m_disconnect.get();
}

// ===============

QMultiMap<QObject*, QtConnectionObject*> QtRuntimeConnectionMethod::connections;

const ClassInfo QtRuntimeConnectionMethod::s_info = { "QtRuntimeMethod", &Base::s_info, 0, 0, CREATE_METHOD_TABLE(QtRuntimeConnectionMethod) };

QtRuntimeConnectionMethod::QtRuntimeConnectionMethod(ExecState* exec, Structure* structure, const UString& identifier)
    : QtRuntimeMethod (new QtRuntimeConnectionMethodData(), exec, structure, identifier)
{
}

void QtRuntimeConnectionMethod::finishCreation(ExecState* exec, const UString& identifier, bool isConnect, PassRefPtr<QtInstance> instance, int index, const QByteArray& signature)
{
    Base::finishCreation(exec, identifier, instance);
    QW_D(QtRuntimeConnectionMethod);

    d->m_signature = signature;
    d->m_index = index;
    d->m_isConnect = isConnect;
}

EncodedJSValue QtRuntimeConnectionMethod::call(ExecState* exec)
{
    QtRuntimeConnectionMethodData* d = static_cast<QtRuntimeConnectionMethod *>(exec->callee())->d_func();

    JSLock lock(SilenceAssertionsOnly);

    QObject* sender = d->m_instance->getObject();

    if (sender) {

        JSObject* thisObject = exec->lexicalGlobalObject()->methodTable()->toThisObject(exec->lexicalGlobalObject(), exec);
        JSObject* funcObject = 0;

        // QtScript checks signalness first, arguments second
        int signalIndex = -1;

        // Make sure the initial index is a signal
        QMetaMethod m = sender->metaObject()->method(d->m_index);
        if (m.methodType() == QMetaMethod::Signal)
            signalIndex = findSignalIndex(sender->metaObject(), d->m_index, d->m_signature);

        if (signalIndex != -1) {
            if (exec->argumentCount() == 1) {
                funcObject = exec->argument(0).toObject(exec);
                CallData callData;
                if (funcObject->methodTable()->getCallData(funcObject, callData) == CallTypeNone) {
                    if (d->m_isConnect)
                        return throwVMError(exec, createTypeError(exec, "QtMetaMethod.connect: target is not a function"));
                    else
                        return throwVMError(exec, createTypeError(exec, "QtMetaMethod.disconnect: target is not a function"));
                }
            } else if (exec->argumentCount() >= 2) {
                if (exec->argument(0).isObject()) {
                    thisObject = exec->argument(0).toObject(exec);

                    // Get the actual function to call
                    JSObject *asObj = exec->argument(1).toObject(exec);
                    CallData callData;
                    if (asObj->methodTable()->getCallData(asObj, callData) != CallTypeNone) {
                        // Function version
                        funcObject = asObj;
                    } else {
                        // Convert it to a string
                        UString funcName = exec->argument(1).toString(exec)->value(exec);
                        Identifier funcIdent(exec, funcName);

                        // ### DropAllLocks
                        // This is resolved at this point in QtScript
                        JSValue val = thisObject->get(exec, funcIdent);
                        JSObject* asFuncObj = val.toObject(exec);

                        if (asFuncObj->methodTable()->getCallData(asFuncObj, callData) != CallTypeNone) {
                            funcObject = asFuncObj;
                        } else {
                            if (d->m_isConnect)
                                return throwVMError(exec, createTypeError(exec, "QtMetaMethod.connect: target is not a function"));
                            else
                                return throwVMError(exec, createTypeError(exec, "QtMetaMethod.disconnect: target is not a function"));
                        }
                    }
                } else {
                    if (d->m_isConnect)
                        return throwVMError(exec, createTypeError(exec, "QtMetaMethod.connect: thisObject is not an object"));
                    else
                        return throwVMError(exec, createTypeError(exec, "QtMetaMethod.disconnect: thisObject is not an object"));
                }
            } else {
                if (d->m_isConnect)
                    return throwVMError(exec, createError(exec, "QtMetaMethod.connect: no arguments given"));
                else
                    return throwVMError(exec, createError(exec, "QtMetaMethod.disconnect: no arguments given"));
            }

            if (d->m_isConnect) {
                // to connect, we need:
                //  target object [from ctor]
                //  target signal index etc. [from ctor]
                //  receiver function [from arguments]
                //  receiver this object [from arguments]

                ExecState* globalExec = exec->lexicalGlobalObject()->globalExec();
                QtConnectionObject* conn = QtConnectionObject::createWithInternalJSC(globalExec, d->m_instance, signalIndex, thisObject, funcObject);
                bool ok = QMetaObject::connect(sender, signalIndex, conn, conn->metaObject()->methodOffset());
                if (!ok) {
                    delete conn;
                    QString msg = QString(QLatin1String("QtMetaMethod.connect: failed to connect to %1::%2()"))
                            .arg(QLatin1String(sender->metaObject()->className()))
                            .arg(QLatin1String(d->m_signature));
                    return throwVMError(exec, createError(exec, msg.toLatin1().constData()));
                }
                else {
                    // Store connection
                    connections.insert(sender, conn);
                }
            } else {
                // Now to find our previous connection object. Hmm.
                QList<QtConnectionObject*> conns = connections.values(sender);
                bool ret = false;

                JSContextRef context = ::toRef(exec);
                JSObjectRef receiver = ::toRef(thisObject);
                JSObjectRef receiverFunction = ::toRef(funcObject);

                foreach(QtConnectionObject* conn, conns) {
                    // Is this the right connection?
                    if (conn->match(context, sender, signalIndex, receiver, receiverFunction)) {
                        // Yep, disconnect it
                        QMetaObject::disconnect(sender, signalIndex, conn, conn->metaObject()->methodOffset());
                        delete conn; // this will also remove it from the map
                        ret = true;
                        break;
                    }
                }

                if (!ret) {
                    QString msg = QString(QLatin1String("QtMetaMethod.disconnect: failed to disconnect from %1::%2()"))
                            .arg(QLatin1String(sender->metaObject()->className()))
                            .arg(QLatin1String(d->m_signature));
                    return throwVMError(exec, createError(exec, msg.toLatin1().constData()));
                }
            }
        } else {
            QString msg = QString(QLatin1String("QtMetaMethod.%1: %2::%3() is not a signal"))
                    .arg(QLatin1String(d->m_isConnect ? "connect": "disconnect"))
                    .arg(QLatin1String(sender->metaObject()->className()))
                    .arg(QLatin1String(d->m_signature));
            return throwVMError(exec, createTypeError(exec, msg.toLatin1().constData()));
        }
    } else {
        return throwVMError(exec, createError(exec, "cannot call function of deleted QObject"));
    }

    return JSValue::encode(jsUndefined());
}

CallType QtRuntimeConnectionMethod::getCallData(JSCell*, CallData& callData)
{
    callData.native.function = call;
    return CallTypeHost;
}

bool QtRuntimeConnectionMethod::getOwnPropertySlot(JSCell* cell, ExecState* exec, PropertyName propertyName, PropertySlot& slot)
{
    QtRuntimeConnectionMethod* thisObject = jsCast<QtRuntimeConnectionMethod*>(cell);
    if (propertyName == exec->propertyNames().length) {
        slot.setCustom(thisObject, thisObject->lengthGetter);
        return true;
    }

    return QtRuntimeMethod::getOwnPropertySlot(thisObject, exec, propertyName, slot);
}

bool QtRuntimeConnectionMethod::getOwnPropertyDescriptor(JSObject* object, ExecState* exec, PropertyName propertyName, PropertyDescriptor& descriptor)
{
    QtRuntimeConnectionMethod* thisObject = jsCast<QtRuntimeConnectionMethod*>(object);
    if (propertyName == exec->propertyNames().length) {
        PropertySlot slot;
        slot.setCustom(thisObject, lengthGetter);
        descriptor.setDescriptor(slot.getValue(exec, propertyName), DontDelete | ReadOnly | DontEnum);
        return true;
    }

    return QtRuntimeMethod::getOwnPropertyDescriptor(thisObject, exec, propertyName, descriptor);
}

void QtRuntimeConnectionMethod::getOwnPropertyNames(JSObject* object, ExecState* exec, PropertyNameArray& propertyNames, EnumerationMode mode)
{
    if (mode == IncludeDontEnumProperties)
        propertyNames.add(exec->propertyNames().length);

    QtRuntimeMethod::getOwnPropertyNames(object, exec, propertyNames, mode);
}

JSValue QtRuntimeConnectionMethod::lengthGetter(ExecState*, JSValue, PropertyName)
{
    // we have one formal argument, and one optional
    return jsNumber(1);
}

// ===============

QtConnectionObject::QtConnectionObject(JSContextRef context, PassRefPtr<QtInstance> senderInstance, int signalIndex, JSObjectRef receiver, JSObjectRef receiverFunction)
    : QObject(senderInstance->getObject())
    , m_context(context)
    , m_senderInstance(senderInstance)
    , m_originalSender(m_senderInstance->getObject())
    , m_signalIndex(signalIndex)
    , m_receiver(receiver)
    , m_receiverFunction(receiverFunction)
{
    JSValueProtect(m_context, m_receiver);
    JSValueProtect(m_context, m_receiverFunction);
}

QtConnectionObject::~QtConnectionObject()
{
    // We can safely use m_originalSender because connection object will never outlive the sender,
    // which is its QObject parent.
    QtRuntimeConnectionMethod::connections.remove(m_originalSender, this);

    JSValueUnprotect(m_context, m_receiver);
    JSValueUnprotect(m_context, m_receiverFunction);
}

// Begin moc-generated code -- modify with care! Check "HAND EDIT" parts
struct qt_meta_stringdata_QtConnectionObject_t {
    QByteArrayData data[3];
    char stringdata[44];
};
#define QT_MOC_LITERAL(idx, ofs, len) { \
    Q_REFCOUNT_INITIALIZE_STATIC, len, 0, 0, \
    offsetof(qt_meta_stringdata_QtConnectionObject_t, stringdata) + ofs \
        - idx * sizeof(QByteArrayData) \
    }
static const qt_meta_stringdata_QtConnectionObject_t qt_meta_stringdata_QtConnectionObject = {
    {
QT_MOC_LITERAL(0, 0, 33),
QT_MOC_LITERAL(1, 34, 7),
QT_MOC_LITERAL(2, 42, 0)
    },
    "JSC::Bindings::QtConnectionObject\0"
    "execute\0\0"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_QtConnectionObject[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   19,    2, 0x0a,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void QtConnectionObject::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        QtConnectionObject *_t = static_cast<QtConnectionObject *>(_o);
        switch (_id) {
        case 0: _t->execute(_a); break; // HAND EDIT: add _a parameter
        default: ;
        }
    }
}

const QMetaObject QtConnectionObject::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_QtConnectionObject.data,
      qt_meta_data_QtConnectionObject, qt_static_metacall, 0, 0 }
};

const QMetaObject *QtConnectionObject::metaObject() const
{
    return &staticMetaObject;
}

void *QtConnectionObject::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_QtConnectionObject.stringdata))
        return static_cast<void*>(const_cast<QtConnectionObject*>(this));
    return QObject::qt_metacast(_clname);
}

int QtConnectionObject::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    return _id;
}
// End of moc-generated code

static bool isJavaScriptFunction(JSObjectRef object)
{
    CallData callData;
    JSObject* jsObject = toJS(object);
    return jsObject->methodTable()->getCallData(jsObject, callData) == CallTypeJS;
}

void QtConnectionObject::execute(void** argv)
{
    QObject* sender = m_senderInstance->getObject();
    if (!sender) {
        qWarning() << "sender deleted, cannot deliver signal";
        return;
    }

    ASSERT(sender == m_originalSender);

    const QMetaObject* meta = sender->metaObject();
    const QMetaMethod method = meta->method(m_signalIndex);

    JSValueRef* ignoredException = 0;
    JSRetainPtr<JSStringRef> lengthProperty(JSStringCreateWithUTF8CString("length"));
    int receiverLength = int(JSValueToNumber(m_context, JSObjectGetProperty(m_context, m_receiverFunction, lengthProperty.get(), ignoredException), ignoredException));
    int argc = qMax(method.parameterCount(), receiverLength);
    WTF::Vector<JSValueRef> args(argc);

    // TODO: remove once conversion functions use JSC API.
    ExecState* exec = ::toJS(m_context);
    RefPtr<RootObject> rootObject = m_senderInstance->rootObject();

    for (int i = 0; i < argc; i++) {
        int argType = method.parameterType(i);
        args[i] = ::toRef(exec, convertQVariantToValue(exec, rootObject, QVariant(argType, argv[i+1])));
    }

    const bool updateQtSender = isJavaScriptFunction(m_receiverFunction);
    if (updateQtSender)
        QtInstance::qtSenderStack()->push(QObject::sender());

    JSObjectCallAsFunction(m_context, m_receiverFunction, m_receiver, argc, args.data(), 0);

    if (updateQtSender)
        QtInstance::qtSenderStack()->pop();
}

bool QtConnectionObject::match(JSContextRef context, QObject* sender, int signalIndex, JSObjectRef receiver, JSObjectRef receiverFunction)
{
    if (sender != m_originalSender || signalIndex != m_signalIndex)
        return false;
    JSValueRef* ignoredException = 0;
    const bool receiverMatch = (!receiver && !m_receiver) || JSValueIsEqual(context, receiver, m_receiver, ignoredException);
    return receiverMatch && JSValueIsEqual(context, receiverFunction, m_receiverFunction, ignoredException);
}

QtConnectionObject* QtConnectionObject::createWithInternalJSC(ExecState* exec, PassRefPtr<QtInstance> senderInstance, int signalIndex, JSObject* receiver, JSObject* receiverFunction)
{
    return new QtConnectionObject(::toRef(exec), senderInstance, signalIndex, ::toRef(receiver), ::toRef(receiverFunction));
}

// ===============

template <typename T> QtArray<T>::QtArray(QList<T> list, QMetaType::Type type, PassRefPtr<RootObject> rootObject)
    : Array(rootObject)
    , m_list(list)
    , m_type(type)
{
    m_length = m_list.count();
}

template <typename T> QtArray<T>::~QtArray ()
{
}

template <typename T> RootObject* QtArray<T>::rootObject() const
{
    return m_rootObject && m_rootObject->isValid() ? m_rootObject.get() : 0;
}

template <typename T> void QtArray<T>::setValueAt(ExecState* exec, unsigned index, JSValue aValue) const
{
    // QtScript sets the value, but doesn't forward it to the original source
    // (e.g. if you do 'object.intList[5] = 6', the object is not updated, but the
    // copy of the list is).
    int dist = -1;
    QVariant val = convertValueToQVariant(exec, aValue, m_type, &dist);

    if (dist >= 0) {
        m_list[index] = val.value<T>();
    }
}


template <typename T> JSValue QtArray<T>::valueAt(ExecState *exec, unsigned int index) const
{
    if (index < m_length) {
        T val = m_list.at(index);
        return convertQVariantToValue(exec, rootObject(), QVariant::fromValue(val));
    }

    return jsUndefined();
}

// ===============

} }
