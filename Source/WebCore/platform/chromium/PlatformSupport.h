/*
 * Copyright (c) 2010, Google Inc. All rights reserved.
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
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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

#ifndef PlatformSupport_h
#define PlatformSupport_h

#if ENABLE(WEB_AUDIO)
#include "AudioBus.h"
#endif

#include "FileSystem.h"
#include "ImageSource.h"
#include "LinkHash.h"
#include "PasteboardPrivate.h"
#include "PluginData.h"

#include <wtf/Forward.h>
#include <wtf/HashSet.h>
#include <wtf/PassRefPtr.h>
#include <wtf/Vector.h>

typedef struct NPObject NPObject;
typedef struct _NPP NPP_t;
typedef NPP_t* NPP;

#if OS(DARWIN)
typedef struct CGFont* CGFontRef;
typedef uintptr_t ATSFontContainerRef;
#ifdef __OBJC__
@class NSFont;
#else
class NSFont;
#endif
#endif // OS(DARWIN)

#if OS(WINDOWS)
typedef struct HFONT__* HFONT;
#endif

namespace WebCore {

class AsyncFileSystem;
class Clipboard;
class Color;
class Cursor;
class Document;
class Frame;
class GeolocationServiceBridge;
class GeolocationServiceChromium;
class GraphicsContext;
class Image;
class IDBFactoryBackendInterface;
class IDBKey;
class IntRect;
class KURL;
class SerializedScriptValue;
class Widget;
class WorkerRunLoop;

struct Cookie;
struct FontRenderStyle;

// PlatformSupport an interface to the embedding layer that lets the embedder
// supply implementations for Platform functionality that WebCore cannot access
// directly (for example, because WebCore is in a sandbox).

class PlatformSupport {
public:
    // Clipboard ----------------------------------------------------------
    static uint64_t clipboardSequenceNumber(PasteboardPrivate::ClipboardBuffer);

    static bool clipboardIsFormatAvailable(PasteboardPrivate::ClipboardFormat, PasteboardPrivate::ClipboardBuffer);
    static HashSet<String> clipboardReadAvailableTypes(PasteboardPrivate::ClipboardBuffer, bool* containsFilenames);

    static String clipboardReadPlainText(PasteboardPrivate::ClipboardBuffer);
    static void clipboardReadHTML(PasteboardPrivate::ClipboardBuffer, String*, KURL*, unsigned* fragmentStart, unsigned* fragmentEnd);
    static PassRefPtr<SharedBuffer> clipboardReadImage(PasteboardPrivate::ClipboardBuffer);
    static String clipboardReadCustomData(PasteboardPrivate::ClipboardBuffer, const String& type);

    // Only the clipboardRead functions take a buffer argument because
    // Chromium currently uses a different technique to write to alternate
    // clipboard buffers.
    static void clipboardWriteSelection(const String&, const KURL&, const String&, bool);
    static void clipboardWritePlainText(const String&);
    static void clipboardWriteURL(const KURL&, const String&);
    static void clipboardWriteImage(NativeImagePtr, const KURL&, const String&);
    static void clipboardWriteDataObject(Clipboard*);

    // Cookies ------------------------------------------------------------
    static void setCookies(const Document*, const KURL&, const String& value);
    static String cookies(const Document*, const KURL&);
    static String cookieRequestHeaderFieldValue(const Document*, const KURL&);
    static bool rawCookies(const Document*, const KURL&, Vector<Cookie>&);
    static void deleteCookie(const Document*, const KURL&, const String& cookieName);
    static bool cookiesEnabled(const Document*);

    // File ---------------------------------------------------------------
    static void revealFolderInOS(const String&);
    static bool fileExists(const String&);
    static bool deleteFile(const String&);
    static bool deleteEmptyDirectory(const String&);
    static bool getFileSize(const String&, long long& result);
    static bool getFileModificationTime(const String&, time_t& result);
    static String directoryName(const String& path);
    static String pathByAppendingComponent(const String& path, const String& component);
    static bool makeAllDirectories(const String& path);
    static String getAbsolutePath(const String&);
    static bool isDirectory(const String&);
    static KURL filePathToURL(const String&);
    static PlatformFileHandle openFile(const String& path, FileOpenMode);
    static void closeFile(PlatformFileHandle&);
    static long long seekFile(PlatformFileHandle, long long offset, FileSeekOrigin);
    static bool truncateFile(PlatformFileHandle, long long offset);
    static int readFromFile(PlatformFileHandle, char* data, int length);
    static int writeToFile(PlatformFileHandle, const char* data, int length);

#if ENABLE(FILE_SYSTEM)
    static PassOwnPtr<AsyncFileSystem> createAsyncFileSystem();
#endif

    // Font ---------------------------------------------------------------
#if OS(WINDOWS)
    static bool ensureFontLoaded(HFONT);
#endif
#if OS(DARWIN)
    static bool loadFont(NSFont* srcFont, CGFontRef*, uint32_t* fontID);
#elif OS(UNIX)
    static void getRenderStyleForStrike(const char* family, int sizeAndStyle, FontRenderStyle* result);
    struct FontFamily {
        String name;
        bool isBold;
        bool isItalic;
    };
    static void getFontFamilyForCharacters(const UChar*, size_t numCharacters, const char* preferredLocale, FontFamily*);
#endif

    // Forms --------------------------------------------------------------
    static void notifyFormStateChanged(const Document*);

    // Databases ----------------------------------------------------------
    // Returns a handle to the DB file and ooptionally a handle to its containing directory
    static PlatformFileHandle databaseOpenFile(const String& vfsFleName, int desiredFlags);
    // Returns a SQLite code (SQLITE_OK = 0, on success)
    static int databaseDeleteFile(const String& vfsFileName, bool syncDir = false);
    // Returns the attributes of the DB file
    static long databaseGetFileAttributes(const String& vfsFileName);
    // Returns the size of the DB file
    static long long databaseGetFileSize(const String& vfsFileName);
    // Returns the space available for the origin
    static long long databaseGetSpaceAvailableForOrigin(const String& originIdentifier);

    // IndexedDB ----------------------------------------------------------
    static PassRefPtr<IDBFactoryBackendInterface> idbFactory();
    // Extracts keyPath from values and returns the corresponding keys.
    static void createIDBKeysFromSerializedValuesAndKeyPath(const Vector<RefPtr<SerializedScriptValue> >& values, const String& keyPath, Vector<RefPtr<IDBKey> >& keys);
    // Injects key via keyPath into value. Returns true on success.
    static PassRefPtr<SerializedScriptValue> injectIDBKeyIntoSerializedValue(PassRefPtr<IDBKey>, PassRefPtr<SerializedScriptValue>, const String& keyPath);

    // JavaScript ---------------------------------------------------------
    static void notifyJSOutOfMemory(Frame*);
    static bool allowScriptDespiteSettings(const KURL& documentURL);

    // LayoutTestMode -----------------------------------------------------
    static bool layoutTestMode();

    // Plugin -------------------------------------------------------------
    static bool plugins(bool refresh, Vector<PluginInfo>*);
    static NPObject* pluginScriptableObject(Widget*);
    static bool popupsAllowed(NPP);

    // Resources ----------------------------------------------------------
#if ENABLE(WEB_AUDIO)
    static PassOwnPtr<AudioBus> decodeAudioFileData(const char* data, size_t, double sampleRate);
#endif

    // Screen -------------------------------------------------------------
    static int screenHorizontalDPI(Widget*);
    static int screenVerticalDPI(Widget*);
    static int screenDepth(Widget*);
    static int screenDepthPerComponent(Widget*);
    static bool screenIsMonochrome(Widget*);
    static IntRect screenRect(Widget*);
    static IntRect screenAvailableRect(Widget*);

    // SharedTimers -------------------------------------------------------
    static void setSharedTimerFiredFunction(void (*func)());
    static void setSharedTimerFireInterval(double);

    // Theming ------------------------------------------------------------
#if OS(WINDOWS)
    static void paintButton(
        GraphicsContext*, int part, int state, int classicState, const IntRect&);
    static void paintMenuList(
        GraphicsContext*, int part, int state, int classicState, const IntRect&);
    static void paintScrollbarArrow(
        GraphicsContext*, int state, int classicState, const IntRect&);
    static void paintScrollbarThumb(
        GraphicsContext*, int part, int state, int classicState, const IntRect&);
    static void paintScrollbarTrack(
        GraphicsContext*, int part, int state, int classicState, const IntRect&, const IntRect& alignRect);
    static void paintSpinButton(
        GraphicsContext*, int part, int state, int classicState, const IntRect&);
    static void paintTextField(
        GraphicsContext*, int part, int state, int classicState, const IntRect&, const Color&, bool fillContentArea, bool drawEdges);
    static void paintTrackbar(
        GraphicsContext*, int part, int state, int classicState, const IntRect&);
    static void paintProgressBar(
        GraphicsContext*, const IntRect& barRect, const IntRect& valueRect, bool determinate, double animatedSeconds);
#elif OS(DARWIN)
    enum ThemePaintState {
        StateDisabled,
        StateInactive,
        StateActive,
        StatePressed,
    };

    enum ThemePaintSize {
        SizeRegular,
        SizeSmall,
    };

    enum ThemePaintScrollbarOrientation {
        ScrollbarOrientationHorizontal,
        ScrollbarOrientationVertical,
    };

    enum ThemePaintScrollbarParent {
        ScrollbarParentScrollView,
        ScrollbarParentRenderLayer,
    };

    struct ThemePaintScrollbarInfo {
        ThemePaintScrollbarOrientation orientation;
        ThemePaintScrollbarParent parent;
        int maxValue;
        int currentValue;
        int visibleSize;
        int totalSize;
    };

    static void paintScrollbarThumb(GraphicsContext*, ThemePaintState, ThemePaintSize, const IntRect&, const ThemePaintScrollbarInfo&);
#elif OS(UNIX)
    // The UI part which is being accessed.
    enum ThemePart {
        // ScrollbarTheme parts
        PartScrollbarDownArrow,
        PartScrollbarLeftArrow,
        PartScrollbarRightArrow,
        PartScrollbarUpArrow,
        PartScrollbarHorizontalThumb,
        PartScrollbarVerticalThumb,
        PartScrollbarHorizontalTrack,
        PartScrollbarVerticalTrack,

        // RenderTheme parts
        PartCheckbox,
        PartRadio,
        PartButton,
        PartTextField,
        PartMenuList,
        PartSliderTrack,
        PartSliderThumb,
        PartInnerSpinButton,
        PartProgressBar
    };

    // The current state of the associated Part.
    enum ThemePaintState {
        StateDisabled,
        StateHover,
        StateNormal,
        StatePressed
    };

    struct ScrollbarTrackExtraParams {
        // The bounds of the entire track, as opposed to the part being painted.
        int trackX;
        int trackY;
        int trackWidth;
        int trackHeight;
    };

    struct ButtonExtraParams {
        bool checked;
        bool indeterminate; // Whether the button state is indeterminate.
        bool isDefault; // Whether the button is default button.
        bool hasBorder;
        unsigned backgroundColor;
    };

    struct TextFieldExtraParams {
        bool isTextArea;
        bool isListbox;
        unsigned backgroundColor;
    };

    struct MenuListExtraParams {
        bool hasBorder;
        bool hasBorderRadius;
        int arrowX;
        int arrowY;
        unsigned backgroundColor;
    };

    struct SliderExtraParams {
        bool vertical;
        bool inDrag;
    };

    struct InnerSpinButtonExtraParams {
        bool spinUp;
        bool readOnly;
    };

    struct ProgressBarExtraParams {
        bool determinate;
        int valueRectX;
        int valueRectY;
        int valueRectWidth;
        int valueRectHeight;
    };

    union ThemePaintExtraParams {
        ScrollbarTrackExtraParams scrollbarTrack;
        ButtonExtraParams button;
        TextFieldExtraParams textField;
        MenuListExtraParams menuList;
        SliderExtraParams slider;
        InnerSpinButtonExtraParams innerSpin;
        ProgressBarExtraParams progressBar;
    };

    // Gets the size of the given theme part. For variable sized items
    // like vertical scrollbar thumbs, the width will be the required width of
    // the track while the height will be the minimum height.
    static IntSize getThemePartSize(ThemePart);
    // Paint the given the given theme part.
    static void paintThemePart(GraphicsContext*, ThemePart, ThemePaintState, const IntRect&, const ThemePaintExtraParams*);
#endif

    // Visited links ------------------------------------------------------
    static LinkHash visitedLinkHash(const UChar* url, unsigned length);
    static LinkHash visitedLinkHash(const KURL& base, const AtomicString& attributeURL);

    static void didStartWorkerRunLoop(WorkerRunLoop*);
    static void didStopWorkerRunLoop(WorkerRunLoop*);

    static bool canAccelerate2dCanvas();
};

} // namespace WebCore

#endif
