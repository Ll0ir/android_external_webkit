/*
 * Copyright 2006, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "webcoreglue"

#include "config.h"
#include "WebViewCore.h"

#include "AccessibilityObject.h"
#include "BaseLayerAndroid.h"
#include "CachedNode.h"
#include "CachedRoot.h"
#include "Chrome.h"
#include "ChromeClientAndroid.h"
#include "Color.h"
#include "DatabaseTracker.h"
#include "Document.h"
#include "DOMWindow.h"
#include "DOMSelection.h"
#include "Element.h"
#include "Editor.h"
#include "EditorClientAndroid.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "ExceptionCode.h"
#include "FocusController.h"
#include "Font.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClientAndroid.h"
#include "FrameTree.h"
#include "FrameView.h"
#include "Geolocation.h"
#include "GraphicsContext.h"
#include "GraphicsJNI.h"
#include "HTMLAnchorElement.h"
#include "HTMLAreaElement.h"
#include "HTMLElement.h"
#include "HTMLFormControlElement.h"
#include "HTMLImageElement.h"
#include "HTMLInputElement.h"
#include "HTMLLabelElement.h"
#include "HTMLMapElement.h"
#include "HTMLNames.h"
#include "HTMLOptGroupElement.h"
#include "HTMLOptionElement.h"
#include "HTMLSelectElement.h"
#include "HTMLTextAreaElement.h"
#include "HistoryItem.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "InlineTextBox.h"
#include "Navigator.h"
#include "Node.h"
#include "NodeList.h"
#include "Page.h"
#include "PageGroup.h"
#include "PlatformKeyboardEvent.h"
#include "PlatformString.h"
#include "PluginWidgetAndroid.h"
#include "PluginView.h"
#include "Position.h"
#include "ProgressTracker.h"
#include "Range.h"
#include "RenderBox.h"
#include "RenderInline.h"
#include "RenderLayer.h"
#include "RenderPart.h"
#include "RenderText.h"
#include "RenderTextControl.h"
#include "RenderThemeAndroid.h"
#include "RenderView.h"
#include "ResourceRequest.h"
#include "SchemeRegistry.h"
#include "SelectionController.h"
#include "Settings.h"
#include "SkANP.h"
#include "SkTemplates.h"
#include "SkTDArray.h"
#include "SkTypes.h"
#include "SkCanvas.h"
#include "SkPicture.h"
#include "SkUtils.h"
#include "Text.h"
#include "TypingCommand.h"
#include "WebCoreFrameBridge.h"
#include "WebFrameView.h"
#include "WindowsKeyboardCodes.h"
#include "android_graphics.h"
#include "autofill/WebAutoFill.h"
#include "markup.h"

#include <JNIHelp.h>
#include <JNIUtility.h>
#include <ui/KeycodeLabels.h>
#include <wtf/CurrentTime.h>
#include <wtf/text/AtomicString.h>
#include <wtf/text/StringImpl.h>

#if USE(V8)
#include "ScriptController.h"
#include "V8Counters.h"
#include <wtf/text/CString.h>
#endif

#if DEBUG_NAV_UI
#include "SkTime.h"
#endif

#if ENABLE(TOUCH_EVENTS) // Android
#include "PlatformTouchEvent.h"
#endif

#ifdef ANDROID_DOM_LOGGING
#include "AndroidLog.h"
#include "RenderTreeAsText.h"
#include <wtf/text/CString.h>

FILE* gDomTreeFile = 0;
FILE* gRenderTreeFile = 0;
#endif

#ifdef ANDROID_INSTRUMENT
#include "TimeCounter.h"
#endif

#if USE(ACCELERATED_COMPOSITING)
#include "GraphicsLayerAndroid.h"
#include "RenderLayerCompositor.h"
#endif

/*  We pass this flag when recording the actual content, so that we don't spend
    time actually regionizing complex path clips, when all we really want to do
    is record them.
 */
#define PICT_RECORD_FLAGS   SkPicture::kUsePathBoundsForClip_RecordingFlag

////////////////////////////////////////////////////////////////////////////////////////////////

namespace android {

static SkTDArray<WebViewCore*> gInstanceList;

void WebViewCore::addInstance(WebViewCore* inst) {
    *gInstanceList.append() = inst;
}

void WebViewCore::removeInstance(WebViewCore* inst) {
    int index = gInstanceList.find(inst);
    LOG_ASSERT(index >= 0, "RemoveInstance inst not found");
    if (index >= 0) {
        gInstanceList.removeShuffle(index);
    }
}

bool WebViewCore::isInstance(WebViewCore* inst) {
    return gInstanceList.find(inst) >= 0;
}

jobject WebViewCore::getApplicationContext() {

    // check to see if there is a valid webviewcore object
    if (gInstanceList.isEmpty())
        return 0;

    // get the context from the webview
    jobject context = gInstanceList[0]->getContext();

    if (!context)
        return 0;

    // get the application context using JNI
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jclass contextClass = env->GetObjectClass(context);
    jmethodID appContextMethod = env->GetMethodID(contextClass, "getApplicationContext", "()Landroid/content/Context;");
    jobject result = env->CallObjectMethod(context, appContextMethod);
    checkException(env);
    return result;
}

// ----------------------------------------------------------------------------

#define GET_NATIVE_VIEW(env, obj) ((WebViewCore*)env->GetIntField(obj, gWebViewCoreFields.m_nativeClass))

// Field ids for WebViewCore
struct WebViewCoreFields {
    jfieldID    m_nativeClass;
    jfieldID    m_viewportWidth;
    jfieldID    m_viewportHeight;
    jfieldID    m_viewportInitialScale;
    jfieldID    m_viewportMinimumScale;
    jfieldID    m_viewportMaximumScale;
    jfieldID    m_viewportUserScalable;
    jfieldID    m_viewportDensityDpi;
    jfieldID    m_webView;
    jfieldID    m_drawIsPaused;
} gWebViewCoreFields;

// ----------------------------------------------------------------------------

struct WebViewCore::JavaGlue {
    jweak       m_obj;
    jmethodID   m_spawnScrollTo;
    jmethodID   m_scrollTo;
    jmethodID   m_scrollBy;
    jmethodID   m_contentDraw;
    jmethodID   m_requestListBox;
    jmethodID   m_openFileChooser;
    jmethodID   m_requestSingleListBox;
    jmethodID   m_jsAlert;
    jmethodID   m_jsConfirm;
    jmethodID   m_jsPrompt;
    jmethodID   m_jsUnload;
    jmethodID   m_jsInterrupt;
    jmethodID   m_didFirstLayout;
    jmethodID   m_updateViewport;
    jmethodID   m_sendNotifyProgressFinished;
    jmethodID   m_sendViewInvalidate;
    jmethodID   m_updateTextfield;
    jmethodID   m_updateTextSelection;
    jmethodID   m_clearTextEntry;
    jmethodID   m_restoreScale;
    jmethodID   m_needTouchEvents;
    jmethodID   m_requestKeyboard;
    jmethodID   m_requestKeyboardWithSelection;
    jmethodID   m_exceededDatabaseQuota;
    jmethodID   m_reachedMaxAppCacheSize;
    jmethodID   m_populateVisitedLinks;
    jmethodID   m_geolocationPermissionsShowPrompt;
    jmethodID   m_geolocationPermissionsHidePrompt;
    jmethodID   m_getDeviceOrientationService;
    jmethodID   m_addMessageToConsole;
    jmethodID   m_formDidBlur;
    jmethodID   m_getPluginClass;
    jmethodID   m_showFullScreenPlugin;
    jmethodID   m_hideFullScreenPlugin;
    jmethodID   m_addSurface;
    jmethodID   m_updateSurface;
    jmethodID   m_destroySurface;
    jmethodID   m_getContext;
    jmethodID   m_sendFindAgain;
    jmethodID   m_showRect;
    jmethodID   m_centerFitRect;
    jmethodID   m_setScrollbarModes;
    jmethodID   m_setInstallableWebApp;
    jmethodID   m_setWebTextViewAutoFillable;
    AutoJObject object(JNIEnv* env) {
        return getRealObject(env, m_obj);
    }
};

/*
 * WebViewCore Implementation
 */

static jmethodID GetJMethod(JNIEnv* env, jclass clazz, const char name[], const char signature[])
{
    jmethodID m = env->GetMethodID(clazz, name, signature);
    LOG_ASSERT(m, "Could not find method %s", name);
    return m;
}

Mutex WebViewCore::gFrameCacheMutex;
Mutex WebViewCore::gButtonMutex;
Mutex WebViewCore::gCursorBoundsMutex;

WebViewCore::WebViewCore(JNIEnv* env, jobject javaWebViewCore, WebCore::Frame* mainframe)
    : m_pluginInvalTimer(this, &WebViewCore::pluginInvalTimerFired)
    , m_deviceOrientationManager(this)
{
    m_mainFrame = mainframe;

    m_popupReply = 0;
    m_moveGeneration = 0;
    m_lastGeneration = 0;
    m_touchGeneration = 0;
    m_blockTextfieldUpdates = false;
    // just initial values. These should be set by client
    m_maxXScroll = 320/4;
    m_maxYScroll = 240/4;
    m_textGeneration = 0;
    m_screenWidth = 320;
    m_textWrapWidth = 320;
    m_scale = 1;
#if ENABLE(TOUCH_EVENTS)
    m_forwardingTouchEvents = false;
#endif
    m_isPaused = false;

    LOG_ASSERT(m_mainFrame, "Uh oh, somehow a frameview was made without an initial frame!");

    jclass clazz = env->GetObjectClass(javaWebViewCore);
    m_javaGlue = new JavaGlue;
    m_javaGlue->m_obj = env->NewWeakGlobalRef(javaWebViewCore);
    m_javaGlue->m_spawnScrollTo = GetJMethod(env, clazz, "contentSpawnScrollTo", "(II)V");
    m_javaGlue->m_scrollTo = GetJMethod(env, clazz, "contentScrollTo", "(II)V");
    m_javaGlue->m_scrollBy = GetJMethod(env, clazz, "contentScrollBy", "(IIZ)V");
    m_javaGlue->m_contentDraw = GetJMethod(env, clazz, "contentDraw", "()V");
    m_javaGlue->m_requestListBox = GetJMethod(env, clazz, "requestListBox", "([Ljava/lang/String;[I[I)V");
    m_javaGlue->m_openFileChooser = GetJMethod(env, clazz, "openFileChooser", "(Ljava/lang/String;)Ljava/lang/String;");
    m_javaGlue->m_requestSingleListBox = GetJMethod(env, clazz, "requestListBox", "([Ljava/lang/String;[II)V");
    m_javaGlue->m_jsAlert = GetJMethod(env, clazz, "jsAlert", "(Ljava/lang/String;Ljava/lang/String;)V");
    m_javaGlue->m_jsConfirm = GetJMethod(env, clazz, "jsConfirm", "(Ljava/lang/String;Ljava/lang/String;)Z");
    m_javaGlue->m_jsPrompt = GetJMethod(env, clazz, "jsPrompt", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    m_javaGlue->m_jsUnload = GetJMethod(env, clazz, "jsUnload", "(Ljava/lang/String;Ljava/lang/String;)Z");
    m_javaGlue->m_jsInterrupt = GetJMethod(env, clazz, "jsInterrupt", "()Z");
    m_javaGlue->m_didFirstLayout = GetJMethod(env, clazz, "didFirstLayout", "(Z)V");
    m_javaGlue->m_updateViewport = GetJMethod(env, clazz, "updateViewport", "()V");
    m_javaGlue->m_sendNotifyProgressFinished = GetJMethod(env, clazz, "sendNotifyProgressFinished", "()V");
    m_javaGlue->m_sendViewInvalidate = GetJMethod(env, clazz, "sendViewInvalidate", "(IIII)V");
    m_javaGlue->m_updateTextfield = GetJMethod(env, clazz, "updateTextfield", "(IZLjava/lang/String;I)V");
    m_javaGlue->m_updateTextSelection = GetJMethod(env, clazz, "updateTextSelection", "(IIII)V");
    m_javaGlue->m_clearTextEntry = GetJMethod(env, clazz, "clearTextEntry", "()V");
    m_javaGlue->m_restoreScale = GetJMethod(env, clazz, "restoreScale", "(II)V");
    m_javaGlue->m_needTouchEvents = GetJMethod(env, clazz, "needTouchEvents", "(Z)V");
    m_javaGlue->m_requestKeyboard = GetJMethod(env, clazz, "requestKeyboard", "(Z)V");
    m_javaGlue->m_requestKeyboardWithSelection = GetJMethod(env, clazz, "requestKeyboardWithSelection", "(IIII)V");
    m_javaGlue->m_exceededDatabaseQuota = GetJMethod(env, clazz, "exceededDatabaseQuota", "(Ljava/lang/String;Ljava/lang/String;JJ)V");
    m_javaGlue->m_reachedMaxAppCacheSize = GetJMethod(env, clazz, "reachedMaxAppCacheSize", "(J)V");
    m_javaGlue->m_populateVisitedLinks = GetJMethod(env, clazz, "populateVisitedLinks", "()V");
    m_javaGlue->m_geolocationPermissionsShowPrompt = GetJMethod(env, clazz, "geolocationPermissionsShowPrompt", "(Ljava/lang/String;)V");
    m_javaGlue->m_geolocationPermissionsHidePrompt = GetJMethod(env, clazz, "geolocationPermissionsHidePrompt", "()V");
    m_javaGlue->m_getDeviceOrientationService = GetJMethod(env, clazz, "getDeviceOrientationService", "()Landroid/webkit/DeviceOrientationService;");
    m_javaGlue->m_addMessageToConsole = GetJMethod(env, clazz, "addMessageToConsole", "(Ljava/lang/String;ILjava/lang/String;I)V");
    m_javaGlue->m_formDidBlur = GetJMethod(env, clazz, "formDidBlur", "(I)V");
    m_javaGlue->m_getPluginClass = GetJMethod(env, clazz, "getPluginClass", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Class;");
    m_javaGlue->m_showFullScreenPlugin = GetJMethod(env, clazz, "showFullScreenPlugin", "(Landroid/webkit/ViewManager$ChildView;I)V");
    m_javaGlue->m_hideFullScreenPlugin = GetJMethod(env, clazz, "hideFullScreenPlugin", "()V");
    m_javaGlue->m_addSurface = GetJMethod(env, clazz, "addSurface", "(Landroid/view/View;IIII)Landroid/webkit/ViewManager$ChildView;");
    m_javaGlue->m_updateSurface = GetJMethod(env, clazz, "updateSurface", "(Landroid/webkit/ViewManager$ChildView;IIII)V");
    m_javaGlue->m_destroySurface = GetJMethod(env, clazz, "destroySurface", "(Landroid/webkit/ViewManager$ChildView;)V");
    m_javaGlue->m_getContext = GetJMethod(env, clazz, "getContext", "()Landroid/content/Context;");
    m_javaGlue->m_sendFindAgain = GetJMethod(env, clazz, "sendFindAgain", "()V");
    m_javaGlue->m_showRect = GetJMethod(env, clazz, "showRect", "(IIIIIIFFFF)V");
    m_javaGlue->m_centerFitRect = GetJMethod(env, clazz, "centerFitRect", "(IIII)V");
    m_javaGlue->m_setScrollbarModes = GetJMethod(env, clazz, "setScrollbarModes", "(II)V");
    m_javaGlue->m_setInstallableWebApp = GetJMethod(env, clazz, "setInstallableWebApp", "()V");
    m_javaGlue->m_setWebTextViewAutoFillable = GetJMethod(env, clazz, "setWebTextViewAutoFillable", "(I)V");

    env->SetIntField(javaWebViewCore, gWebViewCoreFields.m_nativeClass, (jint)this);

    m_scrollOffsetX = m_scrollOffsetY = 0;

    PageGroup::setShouldTrackVisitedLinks(true);

    reset(true);

    WebViewCore::addInstance(this);
}

WebViewCore::~WebViewCore()
{
    WebViewCore::removeInstance(this);

    // Release the focused view
    Release(m_popupReply);

    if (m_javaGlue->m_obj) {
        JNIEnv* env = JSC::Bindings::getJNIEnv();
        env->DeleteWeakGlobalRef(m_javaGlue->m_obj);
        m_javaGlue->m_obj = 0;
    }
    delete m_javaGlue;
    delete m_frameCacheKit;
    delete m_navPictureKit;
}

WebViewCore* WebViewCore::getWebViewCore(const WebCore::FrameView* view)
{
    return getWebViewCore(static_cast<const WebCore::ScrollView*>(view));
}

WebViewCore* WebViewCore::getWebViewCore(const WebCore::ScrollView* view)
{
    if (!view)
        return 0;

    WebFrameView* webFrameView = static_cast<WebFrameView*>(view->platformWidget());
    if (!webFrameView)
        return 0;
    return webFrameView->webViewCore();
}

void WebViewCore::reset(bool fromConstructor)
{
    DBG_SET_LOG("");
    if (fromConstructor) {
        m_frameCacheKit = 0;
        m_navPictureKit = 0;
    } else {
        gFrameCacheMutex.lock();
        delete m_frameCacheKit;
        delete m_navPictureKit;
        m_frameCacheKit = 0;
        m_navPictureKit = 0;
        gFrameCacheMutex.unlock();
    }

    m_lastFocused = 0;
    m_lastFocusedBounds = WebCore::IntRect(0,0,0,0);
    m_focusBoundsChanged = false;
    m_lastFocusedSelStart = 0;
    m_lastFocusedSelEnd = 0;
    clearContent();
    m_updatedFrameCache = true;
    m_frameCacheOutOfDate = true;
    m_skipContentDraw = false;
    m_findIsUp = false;
    m_domtree_version = 0;
    m_check_domtree_version = true;
    m_progressDone = false;
    m_hasCursorBounds = false;

    m_scrollOffsetX = 0;
    m_scrollOffsetY = 0;
    m_screenWidth = 0;
    m_screenHeight = 0;
    m_groupForVisitedLinks = NULL;
    m_currentNodeDomNavigationAxis = 0;
}

static bool layoutIfNeededRecursive(WebCore::Frame* f)
{
    if (!f)
        return true;

    WebCore::FrameView* v = f->view();
    if (!v)
        return true;

    if (v->needsLayout())
        v->layout(f->tree()->parent());

    WebCore::Frame* child = f->tree()->firstChild();
    bool success = true;
    while (child) {
        success &= layoutIfNeededRecursive(child);
        child = child->tree()->nextSibling();
    }

    return success && !v->needsLayout();
}

CacheBuilder& WebViewCore::cacheBuilder()
{
    return FrameLoaderClientAndroid::get(m_mainFrame)->getCacheBuilder();
}

WebCore::Node* WebViewCore::currentFocus()
{
    return cacheBuilder().currentFocus();
}

void WebViewCore::recordPicture(SkPicture* picture)
{
    // if there is no document yet, just return
    if (!m_mainFrame->document()) {
        DBG_NAV_LOG("no document");
        return;
    }
    // Call layout to ensure that the contentWidth and contentHeight are correct
    if (!layoutIfNeededRecursive(m_mainFrame)) {
        DBG_NAV_LOG("layout failed");
        return;
    }
    // draw into the picture's recording canvas
    WebCore::FrameView* view = m_mainFrame->view();
    DBG_NAV_LOGD("view=(w=%d,h=%d)", view->contentsWidth(),
        view->contentsHeight());
    SkAutoPictureRecord arp(picture, view->contentsWidth(),
                            view->contentsHeight(), PICT_RECORD_FLAGS);
    SkAutoMemoryUsageProbe mup(__FUNCTION__);

    // Copy m_buttons so we can pass it to our graphics context.
    gButtonMutex.lock();
    WTF::Vector<Container> buttons(m_buttons);
    gButtonMutex.unlock();

    WebCore::PlatformGraphicsContext pgc(arp.getRecordingCanvas(), &buttons);
    WebCore::GraphicsContext gc(&pgc);
    view->platformWidget()->draw(&gc, WebCore::IntRect(0, 0,
        view->contentsWidth(), view->contentsHeight()));

    gButtonMutex.lock();
    updateButtonList(&buttons);
    gButtonMutex.unlock();
}

void WebViewCore::recordPictureSet(PictureSet* content)
{
    // if there is no document yet, just return
    if (!m_mainFrame->document()) {
        DBG_SET_LOG("!m_mainFrame->document()");
        return;
    }
    if (m_addInval.isEmpty()) {
        DBG_SET_LOG("m_addInval.isEmpty()");
        return;
    }
    // Call layout to ensure that the contentWidth and contentHeight are correct
    // it's fine for layout to gather invalidates, but defeat sending a message
    // back to java to call webkitDraw, since we're already in the middle of
    // doing that
    m_skipContentDraw = true;
    bool success = layoutIfNeededRecursive(m_mainFrame);
    m_skipContentDraw = false;

    // We may be mid-layout and thus cannot draw.
    if (!success)
        return;

    {   // collect WebViewCoreRecordTimeCounter after layoutIfNeededRecursive
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreRecordTimeCounter);
#endif

    // if the webkit page dimensions changed, discard the pictureset and redraw.
    WebCore::FrameView* view = m_mainFrame->view();
    int width = view->contentsWidth();
    int height = view->contentsHeight();

    // Use the contents width and height as a starting point.
    SkIRect contentRect;
    contentRect.set(0, 0, width, height);
    SkIRect total(contentRect);

    // Traverse all the frames and add their sizes if they are in the visible
    // rectangle.
    for (WebCore::Frame* frame = m_mainFrame->tree()->traverseNext(); frame;
            frame = frame->tree()->traverseNext()) {
        // If the frame doesn't have an owner then it is the top frame and the
        // view size is the frame size.
        WebCore::RenderPart* owner = frame->ownerRenderer();
        if (owner && owner->style()->visibility() == VISIBLE) {
            int x = owner->x();
            int y = owner->y();

            // Traverse the tree up to the parent to find the absolute position
            // of this frame.
            WebCore::Frame* parent = frame->tree()->parent();
            while (parent) {
                WebCore::RenderPart* parentOwner = parent->ownerRenderer();
                if (parentOwner) {
                    x += parentOwner->x();
                    y += parentOwner->y();
                }
                parent = parent->tree()->parent();
            }
            // Use the owner dimensions so that padding and border are
            // included.
            int right = x + owner->width();
            int bottom = y + owner->height();
            SkIRect frameRect = {x, y, right, bottom};
            // Ignore a width or height that is smaller than 1. Some iframes
            // have small dimensions in order to be hidden. The iframe
            // expansion code does not expand in that case so we should ignore
            // them here.
            if (frameRect.width() > 1 && frameRect.height() > 1
                    && SkIRect::Intersects(total, frameRect))
                total.join(x, y, right, bottom);
        }
    }

    // If the new total is larger than the content, resize the view to include
    // all the content.
    if (!contentRect.contains(total)) {
        // Resize the view to change the overflow clip.
        view->resize(total.fRight, total.fBottom);

        // We have to force a layout in order for the clip to change.
        m_mainFrame->contentRenderer()->setNeedsLayoutAndPrefWidthsRecalc();
        view->forceLayout();

        // Relayout similar to above
        m_skipContentDraw = true;
        bool success = layoutIfNeededRecursive(m_mainFrame);
        m_skipContentDraw = false;
        if (!success)
            return;

        // Set the computed content width
        width = view->contentsWidth();
        height = view->contentsHeight();
    }

    if (cacheBuilder().pictureSetDisabled())
        content->clear();

    content->checkDimensions(width, height, &m_addInval);

    // The inval region may replace existing pictures. The existing pictures
    // may have already been split into pieces. If reuseSubdivided() returns
    // true, the split pieces are the last entries in the picture already. They
    // are marked as invalid, and are rebuilt by rebuildPictureSet().

    // If the new region doesn't match a set of split pieces, add it to the end.
    if (!content->reuseSubdivided(m_addInval)) {
        const SkIRect& inval = m_addInval.getBounds();
        SkPicture* picture = rebuildPicture(inval);
        DBG_SET_LOGD("{%d,%d,w=%d,h=%d}", inval.fLeft,
            inval.fTop, inval.width(), inval.height());
        content->add(m_addInval, picture, 0, false);
        picture->safeUnref();
    }
    // Remove any pictures already in the set that are obscured by the new one,
    // and check to see if any already split pieces need to be redrawn.
    if (content->build())
        rebuildPictureSet(content);
    } // WebViewCoreRecordTimeCounter
    WebCore::Node* oldFocusNode = currentFocus();
    m_frameCacheOutOfDate = true;
    WebCore::IntRect oldBounds;
    int oldSelStart = 0;
    int oldSelEnd = 0;
    if (oldFocusNode) {
        oldBounds = oldFocusNode->getRect();
        RenderObject* renderer = oldFocusNode->renderer();
        if (renderer && (renderer->isTextArea() || renderer->isTextField())) {
            WebCore::RenderTextControl* rtc =
                static_cast<WebCore::RenderTextControl*>(renderer);
            oldSelStart = rtc->selectionStart();
            oldSelEnd = rtc->selectionEnd();
        }
    } else
        oldBounds = WebCore::IntRect(0,0,0,0);
    unsigned latestVersion = 0;
    if (m_check_domtree_version) {
        // as domTreeVersion only increment, we can just check the sum to see
        // whether we need to update the frame cache
        for (Frame* frame = m_mainFrame; frame; frame = frame->tree()->traverseNext()) {
            latestVersion += frame->document()->domTreeVersion();
        }
    }
    DBG_NAV_LOGD("m_lastFocused=%p oldFocusNode=%p"
        " m_lastFocusedBounds={%d,%d,%d,%d} oldBounds={%d,%d,%d,%d}"
        " m_lastFocusedSelection={%d,%d} oldSelection={%d,%d}"
        " m_check_domtree_version=%s latestVersion=%d m_domtree_version=%d",
        m_lastFocused, oldFocusNode,
        m_lastFocusedBounds.x(), m_lastFocusedBounds.y(),
        m_lastFocusedBounds.width(), m_lastFocusedBounds.height(),
        oldBounds.x(), oldBounds.y(), oldBounds.width(), oldBounds.height(),
        m_lastFocusedSelStart, m_lastFocusedSelEnd, oldSelStart, oldSelEnd,
        m_check_domtree_version ? "true" : "false",
        latestVersion, m_domtree_version);
    if (m_lastFocused == oldFocusNode && m_lastFocusedBounds == oldBounds
            && m_lastFocusedSelStart == oldSelStart
            && m_lastFocusedSelEnd == oldSelEnd
            && !m_findIsUp
            && (!m_check_domtree_version || latestVersion == m_domtree_version))
    {
        return;
    }
    m_focusBoundsChanged |= m_lastFocused == oldFocusNode
        && m_lastFocusedBounds != oldBounds;
    m_lastFocused = oldFocusNode;
    m_lastFocusedBounds = oldBounds;
    m_lastFocusedSelStart = oldSelStart;
    m_lastFocusedSelEnd = oldSelEnd;
    m_domtree_version = latestVersion;
    DBG_NAV_LOG("call updateFrameCache");
    updateFrameCache();
    if (m_findIsUp) {
        LOG_ASSERT(m_javaGlue->m_obj,
                "A Java widget was not associated with this view bridge!");
        JNIEnv* env = JSC::Bindings::getJNIEnv();
        env->CallVoidMethod(m_javaGlue->object(env).get(),
                m_javaGlue->m_sendFindAgain);
        checkException(env);
    }
}

void WebViewCore::updateButtonList(WTF::Vector<Container>* buttons)
{
    // All the entries in buttons are either updates of previous entries in
    // m_buttons or they need to be added to it.
    Container* end = buttons->end();
    for (Container* updatedContainer = buttons->begin();
            updatedContainer != end; updatedContainer++) {
        bool updated = false;
        // Search for a previous entry that references the same node as our new
        // data
        Container* lastPossibleMatch = m_buttons.end();
        for (Container* possibleMatch = m_buttons.begin();
                possibleMatch != lastPossibleMatch; possibleMatch++) {
            if (updatedContainer->matches(possibleMatch->node())) {
                // Update our record, and skip to the next one.
                possibleMatch->setRect(updatedContainer->rect());
                updated = true;
                break;
            }
        }
        if (!updated) {
            // This is a brand new button, so append it to m_buttons
            m_buttons.append(*updatedContainer);
        }
    }
    size_t i = 0;
    // count will decrease each time one is removed, so check count each time.
    while (i < m_buttons.size()) {
        if (m_buttons[i].canBeRemoved()) {
            m_buttons[i] = m_buttons.last();
            m_buttons.removeLast();
        } else {
            i++;
        }
    }
}

// note: updateCursorBounds is called directly by the WebView thread
// This needs to be called each time we call CachedRoot::setCursor() with
// non-null CachedNode/CachedFrame, since otherwise the WebViewCore's data
// about the cursor is incorrect.  When we call setCursor(0,0), we need
// to set hasCursorBounds to false.
void WebViewCore::updateCursorBounds(const CachedRoot* root,
        const CachedFrame* cachedFrame, const CachedNode* cachedNode)
{
    LOG_ASSERT(root, "updateCursorBounds: root cannot be null");
    LOG_ASSERT(cachedNode, "updateCursorBounds: cachedNode cannot be null");
    LOG_ASSERT(cachedFrame, "updateCursorBounds: cachedFrame cannot be null");
    gCursorBoundsMutex.lock();
    m_hasCursorBounds = !cachedNode->isHidden();
    // If m_hasCursorBounds is false, we never look at the other
    // values, so do not bother setting them.
    if (m_hasCursorBounds) {
        WebCore::IntRect bounds = cachedNode->bounds(cachedFrame);
        if (m_cursorBounds != bounds)
            DBG_NAV_LOGD("new cursor bounds=(%d,%d,w=%d,h=%d)",
                bounds.x(), bounds.y(), bounds.width(), bounds.height());
        m_cursorBounds = bounds;
        m_cursorHitBounds = cachedNode->hitBounds(cachedFrame);
        m_cursorFrame = cachedFrame->framePointer();
        root->getSimulatedMousePosition(&m_cursorLocation);
        m_cursorNode = cachedNode->nodePointer();
    }
    gCursorBoundsMutex.unlock();
}

void WebViewCore::clearContent()
{
    DBG_SET_LOG("");
    m_content.clear();
    m_addInval.setEmpty();
    m_rebuildInval.setEmpty();
}

bool WebViewCore::focusBoundsChanged()
{
    bool result = m_focusBoundsChanged;
    m_focusBoundsChanged = false;
    return result;
}

SkPicture* WebViewCore::rebuildPicture(const SkIRect& inval)
{
    WebCore::FrameView* view = m_mainFrame->view();
    int width = view->contentsWidth();
    int height = view->contentsHeight();
    SkPicture* picture = new SkPicture();
    SkAutoPictureRecord arp(picture, width, height, PICT_RECORD_FLAGS);
    SkAutoMemoryUsageProbe mup(__FUNCTION__);
    SkCanvas* recordingCanvas = arp.getRecordingCanvas();

    gButtonMutex.lock();
    WTF::Vector<Container> buttons(m_buttons);
    gButtonMutex.unlock();

    WebCore::PlatformGraphicsContext pgc(recordingCanvas, &buttons);
    WebCore::GraphicsContext gc(&pgc);
    recordingCanvas->translate(-inval.fLeft, -inval.fTop);
    recordingCanvas->save();
    view->platformWidget()->draw(&gc, WebCore::IntRect(inval.fLeft,
        inval.fTop, inval.width(), inval.height()));
    m_rebuildInval.op(inval, SkRegion::kUnion_Op);
    DBG_SET_LOGD("m_rebuildInval={%d,%d,r=%d,b=%d}",
        m_rebuildInval.getBounds().fLeft, m_rebuildInval.getBounds().fTop,
        m_rebuildInval.getBounds().fRight, m_rebuildInval.getBounds().fBottom);

    gButtonMutex.lock();
    updateButtonList(&buttons);
    gButtonMutex.unlock();

    return picture;
}

void WebViewCore::rebuildPictureSet(PictureSet* pictureSet)
{
    WebCore::FrameView* view = m_mainFrame->view();
    size_t size = pictureSet->size();
    for (size_t index = 0; index < size; index++) {
        if (pictureSet->upToDate(index))
            continue;
        const SkIRect& inval = pictureSet->bounds(index);
        DBG_SET_LOGD("pictSet=%p [%d] {%d,%d,w=%d,h=%d}", pictureSet, index,
            inval.fLeft, inval.fTop, inval.width(), inval.height());
        pictureSet->setPicture(index, rebuildPicture(inval));
    }
    pictureSet->validate(__FUNCTION__);
}

BaseLayerAndroid* WebViewCore::recordContent(SkRegion* region, SkIPoint* point)
{
    DBG_SET_LOG("start");
    float progress = (float) m_mainFrame->page()->progress()->estimatedProgress();
    m_progressDone = progress <= 0.0f || progress >= 1.0f;
    recordPictureSet(&m_content);
    if (!m_progressDone && m_content.isEmpty()) {
        DBG_SET_LOGD("empty (progress=%g)", progress);
        return 0;
    }
    region->set(m_addInval);
    m_addInval.setEmpty();
    region->op(m_rebuildInval, SkRegion::kUnion_Op);
    m_rebuildInval.setEmpty();
    point->fX = m_content.width();
    point->fY = m_content.height();
    DBG_SET_LOGD("region={%d,%d,r=%d,b=%d}", region->getBounds().fLeft,
        region->getBounds().fTop, region->getBounds().fRight,
        region->getBounds().fBottom);
    DBG_SET_LOG("end");

    BaseLayerAndroid* base = new BaseLayerAndroid();
    base->setContent(m_content);

#if USE(ACCELERATED_COMPOSITING)
    // We update the layers
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(m_mainFrame->page()->chrome()->client());
    GraphicsLayerAndroid* root = static_cast<GraphicsLayerAndroid*>(chromeC->layersSync());
    if (root) {
        root->notifyClientAnimationStarted();
        LayerAndroid* copyLayer = new LayerAndroid(*root->contentLayer());
        base->addChild(copyLayer);
        copyLayer->unref();
    }
#endif

    return base;
}

void WebViewCore::splitContent(PictureSet* content)
{
    bool layoutSuceeded = layoutIfNeededRecursive(m_mainFrame);
    LOG_ASSERT(layoutSuceeded, "Can never be called recursively");
    content->split(&m_content);
    rebuildPictureSet(&m_content);
    content->set(m_content);
}

void WebViewCore::scrollTo(int x, int y, bool animate)
{
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

//    LOGD("WebViewCore::scrollTo(%d %d)\n", x, y);

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            animate ? m_javaGlue->m_spawnScrollTo : m_javaGlue->m_scrollTo,
            x, y);
    checkException(env);
}

void WebViewCore::sendNotifyProgressFinished()
{
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_sendNotifyProgressFinished);
    checkException(env);
}

void WebViewCore::viewInvalidate(const WebCore::IntRect& rect)
{
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
                        m_javaGlue->m_sendViewInvalidate,
                        rect.x(), rect.y(), rect.right(), rect.bottom());
    checkException(env);
}

void WebViewCore::scrollBy(int dx, int dy, bool animate)
{
    if (!(dx | dy))
        return;
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_scrollBy,
        dx, dy, animate);
    checkException(env);
}

void WebViewCore::contentDraw()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_contentDraw);
    checkException(env);
}

void WebViewCore::contentInvalidate(const WebCore::IntRect &r)
{
    DBG_SET_LOGD("rect={%d,%d,w=%d,h=%d}", r.x(), r.y(), r.width(), r.height());
    SkIRect rect(r);
    if (!rect.intersect(0, 0, INT_MAX, INT_MAX))
        return;
    m_addInval.op(rect, SkRegion::kUnion_Op);
    DBG_SET_LOGD("m_addInval={%d,%d,r=%d,b=%d}",
        m_addInval.getBounds().fLeft, m_addInval.getBounds().fTop,
        m_addInval.getBounds().fRight, m_addInval.getBounds().fBottom);
    if (!m_skipContentDraw)
        contentDraw();
}

void WebViewCore::contentInvalidateAll()
{
    WebCore::FrameView* view = m_mainFrame->view();
    contentInvalidate(WebCore::IntRect(0, 0,
        view->contentsWidth(), view->contentsHeight()));
}

void WebViewCore::offInvalidate(const WebCore::IntRect &r)
{
    // FIXME: these invalidates are offscreen, and can be throttled or
    // deferred until the area is visible. For now, treat them as
    // regular invals so that drawing happens (inefficiently) for now.
    contentInvalidate(r);
}

static int pin_pos(int x, int width, int targetWidth)
{
    if (x + width > targetWidth)
        x = targetWidth - width;
    if (x < 0)
        x = 0;
    return x;
}

void WebViewCore::didFirstLayout()
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    WebCore::FrameLoader* loader = m_mainFrame->loader();
    const WebCore::KURL& url = loader->url();
    if (url.isEmpty())
        return;
    LOGV("::WebCore:: didFirstLayout %s", url.string().ascii().data());

    WebCore::FrameLoadType loadType = loader->loadType();

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_didFirstLayout,
            loadType == WebCore::FrameLoadTypeStandard
            // When redirect with locked history, we would like to reset the
            // scale factor. This is important for www.yahoo.com as it is
            // redirected to www.yahoo.com/?rs=1 on load.
            || loadType == WebCore::FrameLoadTypeRedirectWithLockedBackForwardList);
    checkException(env);

    DBG_NAV_LOG("call updateFrameCache");
    m_check_domtree_version = false;
    updateFrameCache();
    m_history.setDidFirstLayout(true);
}

void WebViewCore::updateViewport()
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_updateViewport);
    checkException(env);
}

void WebViewCore::restoreScale(int scale, int textWrapScale)
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_restoreScale, scale, textWrapScale);
    checkException(env);
}

void WebViewCore::needTouchEvents(bool need)
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

#if ENABLE(TOUCH_EVENTS)
    if (m_forwardingTouchEvents == need)
        return;

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_needTouchEvents, need);
    checkException(env);

    m_forwardingTouchEvents = need;
#endif
}

void WebViewCore::requestKeyboardWithSelection(const WebCore::Node* node,
        int selStart, int selEnd)
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_requestKeyboardWithSelection,
            reinterpret_cast<int>(node), selStart, selEnd, m_textGeneration);
    checkException(env);
}

void WebViewCore::requestKeyboard(bool showKeyboard)
{
    DEBUG_NAV_UI_LOGD("%s", __FUNCTION__);
    LOG_ASSERT(m_javaGlue->m_obj, "A Java widget was not associated with this view bridge!");

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_requestKeyboard, showKeyboard);
    checkException(env);
}

void WebViewCore::notifyProgressFinished()
{
    DBG_NAV_LOG("call updateFrameCache");
    m_check_domtree_version = true;
    updateFrameCache();
    sendNotifyProgressFinished();
}

void WebViewCore::doMaxScroll(CacheBuilder::Direction dir)
{
    int dx = 0, dy = 0;

    switch (dir) {
    case CacheBuilder::LEFT:
        dx = -m_maxXScroll;
        break;
    case CacheBuilder::UP:
        dy = -m_maxYScroll;
        break;
    case CacheBuilder::RIGHT:
        dx = m_maxXScroll;
        break;
    case CacheBuilder::DOWN:
        dy = m_maxYScroll;
        break;
    case CacheBuilder::UNINITIALIZED:
    default:
        LOG_ASSERT(0, "unexpected focus selector");
    }
    this->scrollBy(dx, dy, true);
}

void WebViewCore::setScrollOffset(int moveGeneration, int dx, int dy)
{
    DBG_NAV_LOGD("{%d,%d} m_scrollOffset=(%d,%d)", dx, dy,
        m_scrollOffsetX, m_scrollOffsetY);
    if (m_scrollOffsetX != dx || m_scrollOffsetY != dy) {
        m_scrollOffsetX = dx;
        m_scrollOffsetY = dy;
        // The visible rect is located within our coordinate space so it
        // contains the actual scroll position. Setting the location makes hit
        // testing work correctly.
        m_mainFrame->view()->platformWidget()->setLocation(m_scrollOffsetX,
                m_scrollOffsetY);
        m_mainFrame->eventHandler()->sendScrollEvent();

        // update the currently visible screen
        sendPluginVisibleScreen();
    }
    gCursorBoundsMutex.lock();
    bool hasCursorBounds = m_hasCursorBounds;
    Frame* frame = (Frame*) m_cursorFrame;
    IntPoint location = m_cursorLocation;
    gCursorBoundsMutex.unlock();
    if (!hasCursorBounds)
        return;
    moveMouseIfLatest(moveGeneration, frame, location.x(), location.y());
}

void WebViewCore::setGlobalBounds(int x, int y, int h, int v)
{
    DBG_NAV_LOGD("{%d,%d}", x, y);
    m_mainFrame->view()->platformWidget()->setWindowBounds(x, y, h, v);
}

void WebViewCore::setSizeScreenWidthAndScale(int width, int height,
    int textWrapWidth, float scale, int screenWidth, int screenHeight,
    int anchorX, int anchorY, bool ignoreHeight)
{
    WebCoreViewBridge* window = m_mainFrame->view()->platformWidget();
    int ow = window->width();
    int oh = window->height();
    window->setSize(width, height);
    window->setVisibleSize(screenWidth, screenHeight);
    if (width != screenWidth) {
        m_mainFrame->view()->setUseFixedLayout(true);
        m_mainFrame->view()->setFixedLayoutSize(IntSize(width, height));
    } else {
        m_mainFrame->view()->setUseFixedLayout(false);
    }
    int osw = m_screenWidth;
    int osh = m_screenHeight;
    int otw = m_textWrapWidth;
    DBG_NAV_LOGD("old:(w=%d,h=%d,sw=%d,scale=%g) new:(w=%d,h=%d,sw=%d,scale=%g)",
        ow, oh, osw, m_scale, width, height, screenWidth, scale);
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_textWrapWidth = textWrapWidth;
    if (scale >= 0) // negative means keep the current scale
        m_scale = scale;
    m_maxXScroll = screenWidth >> 2;
    m_maxYScroll = m_maxXScroll * height / width;
    if (ow != width || (!ignoreHeight && oh != height) || otw != textWrapWidth) {
        WebCore::RenderObject *r = m_mainFrame->contentRenderer();
        DBG_NAV_LOGD("renderer=%p view=(w=%d,h=%d)", r,
                screenWidth, screenHeight);
        if (r) {
            WebCore::IntPoint anchorPoint = WebCore::IntPoint(anchorX, anchorY);
            DBG_NAV_LOGD("anchorX=%d anchorY=%d", anchorX, anchorY);
            RefPtr<WebCore::Node> node;
            WebCore::IntRect bounds;
            WebCore::IntPoint offset;
            // If the text wrap changed, it is probably zoom change or
            // orientation change. Try to keep the anchor at the same place.
            if (otw && textWrapWidth && otw != textWrapWidth) {
                WebCore::HitTestResult hitTestResult =
                        m_mainFrame->eventHandler()-> hitTestResultAtPoint(
                                anchorPoint, false);
                node = hitTestResult.innerNode();
            }
            if (node) {
                bounds = node->getRect();
                DBG_NAV_LOGD("ob:(x=%d,y=%d,w=%d,h=%d)",
                    bounds.x(), bounds.y(), bounds.width(), bounds.height());
                // sites like nytimes.com insert a non-standard tag <nyt_text>
                // in the html. If it is the HitTestResult, it may have zero
                // width and height. In this case, use its parent node.
                if (bounds.width() == 0) {
                    node = node->parent();
                    if (node) {
                        bounds = node->getRect();
                        DBG_NAV_LOGD("found a zero width node and use its parent, whose ob:(x=%d,y=%d,w=%d,h=%d)",
                                bounds.x(), bounds.y(), bounds.width(), bounds.height());
                    }
                }
            }
            r->setNeedsLayoutAndPrefWidthsRecalc();
            m_mainFrame->view()->forceLayout();
            // scroll to restore current screen center
            if (node) {
                const WebCore::IntRect& newBounds = node->getRect();
                DBG_NAV_LOGD("nb:(x=%d,y=%d,w=%d,"
                    "h=%d)", newBounds.x(), newBounds.y(),
                    newBounds.width(), newBounds.height());
                if ((osw && osh && bounds.width() && bounds.height())
                    && (bounds != newBounds)) {
                    WebCore::FrameView* view = m_mainFrame->view();
                    // force left align if width is not changed while height changed.
                    // the anchorPoint is probably at some white space in the node
                    // which is affected by text wrap around the screen width.
                    const bool leftAlign = (otw != textWrapWidth)
                        && (bounds.width() == newBounds.width())
                        && (bounds.height() != newBounds.height());
                    const float xPercentInDoc =
                        leftAlign ? 0.0 : (float) (anchorX - bounds.x()) / bounds.width();
                    const float xPercentInView =
                        leftAlign ? 0.0 : (float) (anchorX - m_scrollOffsetX) / osw;
                    const float yPercentInDoc = (float) (anchorY - bounds.y()) / bounds.height();
                    const float yPercentInView = (float) (anchorY - m_scrollOffsetY) / osh;
                    showRect(newBounds.x(), newBounds.y(), newBounds.width(),
                             newBounds.height(), view->contentsWidth(),
                             view->contentsHeight(),
                             xPercentInDoc, xPercentInView,
                             yPercentInDoc, yPercentInView);
                }
            }
        }
    }

    // update the currently visible screen as perceived by the plugin
    sendPluginVisibleScreen();
}

void WebViewCore::dumpDomTree(bool useFile)
{
#ifdef ANDROID_DOM_LOGGING
    if (useFile)
        gDomTreeFile = fopen(DOM_TREE_LOG_FILE, "w");
    m_mainFrame->document()->showTreeForThis();
    if (gDomTreeFile) {
        fclose(gDomTreeFile);
        gDomTreeFile = 0;
    }
#endif
}

void WebViewCore::dumpRenderTree(bool useFile)
{
#ifdef ANDROID_DOM_LOGGING
    WTF::CString renderDump = WebCore::externalRepresentation(m_mainFrame).utf8();
    const char* data = renderDump.data();
    if (useFile) {
        gRenderTreeFile = fopen(RENDER_TREE_LOG_FILE, "w");
        DUMP_RENDER_LOGD("%s", data);
        fclose(gRenderTreeFile);
        gRenderTreeFile = 0;
    } else {
        // adb log can only output 1024 characters, so write out line by line.
        // exclude '\n' as adb log adds it for each output.
        int length = renderDump.length();
        for (int i = 0, last = 0; i < length; i++) {
            if (data[i] == '\n') {
                if (i != last)
                    DUMP_RENDER_LOGD("%.*s", (i - last), &(data[last]));
                last = i + 1;
            }
        }
    }
#endif
}

void WebViewCore::dumpNavTree()
{
#if DUMP_NAV_CACHE
    cacheBuilder().mDebug.print();
#endif
}

WebCore::HTMLAnchorElement* WebViewCore::retrieveAnchorElement(WebCore::Frame* frame, WebCore::Node* node)
{
    if (!CacheBuilder::validNode(m_mainFrame, frame, node))
        return 0;
    if (!node->hasTagName(WebCore::HTMLNames::aTag))
        return 0;
    return static_cast<WebCore::HTMLAnchorElement*>(node);
}

WTF::String WebViewCore::retrieveHref(WebCore::Frame* frame, WebCore::Node* node)
{
    WebCore::HTMLAnchorElement* anchor = retrieveAnchorElement(frame, node);
    return anchor ? anchor->href() : WTF::String();
}

WTF::String WebViewCore::retrieveAnchorText(WebCore::Frame* frame, WebCore::Node* node)
{
    WebCore::HTMLAnchorElement* anchor = retrieveAnchorElement(frame, node);
    return anchor ? anchor->text() : WTF::String();
}

WTF::String WebViewCore::requestLabel(WebCore::Frame* frame,
        WebCore::Node* node)
{
    if (node && CacheBuilder::validNode(m_mainFrame, frame, node)) {
        RefPtr<WebCore::NodeList> list = node->document()->getElementsByTagName("label");
        unsigned length = list->length();
        for (unsigned i = 0; i < length; i++) {
            WebCore::HTMLLabelElement* label = static_cast<WebCore::HTMLLabelElement*>(
                    list->item(i));
            if (label->control() == node) {
                Node* node = label;
                String result;
                while ((node = node->traverseNextNode(label))) {
                    if (node->isTextNode()) {
                        Text* textNode = static_cast<Text*>(node);
                        result += textNode->dataImpl();
                    }
                }
                return result;
            }
        }
    }
    return WTF::String();
}

void WebViewCore::revealSelection()
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    WebCore::RenderObject* renderer = focus->renderer();
    if ((!renderer || (!renderer->isTextField() && !renderer->isTextArea()))
        && !focus->isContentEditable())
        return;
    WebCore::Frame* focusedFrame = focus->document()->frame();
    WebFrame* webFrame = WebFrame::getWebFrame(focusedFrame);
    webFrame->setUserInitiatedAction(true);
    focusedFrame->selection()->revealSelection();
    webFrame->setUserInitiatedAction(false);
}

void WebViewCore::updateCacheOnNodeChange()
{
    gCursorBoundsMutex.lock();
    bool hasCursorBounds = m_hasCursorBounds;
    Frame* frame = (Frame*) m_cursorFrame;
    Node* node = (Node*) m_cursorNode;
    IntRect bounds = m_cursorHitBounds;
    gCursorBoundsMutex.unlock();
    if (!hasCursorBounds || !node)
        return;
    if (CacheBuilder::validNode(m_mainFrame, frame, node)) {
        RenderObject* renderer = node->renderer();
        if (renderer && renderer->style()->visibility() != HIDDEN) {
            IntRect absBox = renderer->absoluteBoundingBoxRect();
            int globalX, globalY;
            CacheBuilder::GetGlobalOffset(frame, &globalX, &globalY);
            absBox.move(globalX, globalY);
            if (absBox == bounds)
                return;
            DBG_NAV_LOGD("absBox=(%d,%d,%d,%d) bounds=(%d,%d,%d,%d)",
                absBox.x(), absBox.y(), absBox.width(), absBox.height(),
                bounds.x(), bounds.y(), bounds.width(), bounds.height());
        }
    }
    DBG_NAV_LOGD("updateFrameCache node=%p", node);
    updateFrameCache();
}

void WebViewCore::updateFrameCache()
{
    if (!m_frameCacheOutOfDate) {
        DBG_NAV_LOG("!m_frameCacheOutOfDate");
        return;
    }
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreBuildNavTimeCounter);
#endif
    m_frameCacheOutOfDate = false;
#if DEBUG_NAV_UI
    m_now = SkTime::GetMSecs();
#endif
    m_temp = new CachedRoot();
    m_temp->init(m_mainFrame, &m_history);
#if USE(ACCELERATED_COMPOSITING)
    GraphicsLayerAndroid* graphicsLayer = graphicsRootLayer();
    if (graphicsLayer)
        m_temp->setRootLayer(graphicsLayer->contentLayer());
#endif
    CacheBuilder& builder = cacheBuilder();
    WebCore::Settings* settings = m_mainFrame->page()->settings();
    builder.allowAllTextDetection();
#ifdef ANDROID_META_SUPPORT
    if (settings) {
        if (!settings->formatDetectionAddress())
            builder.disallowAddressDetection();
        if (!settings->formatDetectionEmail())
            builder.disallowEmailDetection();
        if (!settings->formatDetectionTelephone())
            builder.disallowPhoneDetection();
    }
#endif
    builder.buildCache(m_temp);
    m_tempPict = new SkPicture();
    recordPicture(m_tempPict);
    m_temp->setPicture(m_tempPict);
    m_temp->setTextGeneration(m_textGeneration);
    WebCoreViewBridge* window = m_mainFrame->view()->platformWidget();
    m_temp->setVisibleRect(WebCore::IntRect(m_scrollOffsetX,
        m_scrollOffsetY, window->width(), window->height()));
    gFrameCacheMutex.lock();
    delete m_frameCacheKit;
    delete m_navPictureKit;
    m_frameCacheKit = m_temp;
    m_navPictureKit = m_tempPict;
    m_updatedFrameCache = true;
#if DEBUG_NAV_UI
    const CachedNode* cachedFocusNode = m_frameCacheKit->currentFocus();
    DBG_NAV_LOGD("cachedFocusNode=%d (nodePointer=%p)",
        cachedFocusNode ? cachedFocusNode->index() : 0,
        cachedFocusNode ? cachedFocusNode->nodePointer() : 0);
#endif
    gFrameCacheMutex.unlock();
}

void WebViewCore::updateFrameCacheIfLoading()
{
    if (!m_check_domtree_version)
        updateFrameCache();
}

struct TouchNodeData {
    Node* mNode;
    IntRect mBounds;
};

// get the bounding box of the Node
static IntRect getAbsoluteBoundingBox(Node* node) {
    IntRect rect;
    RenderObject* render = node->renderer();
    if (render->isRenderInline())
        rect = toRenderInline(render)->linesVisibleOverflowBoundingBox();
    else if (render->isBox())
        rect = toRenderBox(render)->visualOverflowRect();
    else if (render->isText())
        rect = toRenderText(render)->linesBoundingBox();
    else
        LOGE("getAbsoluteBoundingBox failed for node %p, name %s", node, render->renderName());
    FloatPoint absPos = render->localToAbsolute();
    rect.move(absPos.x(), absPos.y());
    return rect;
}

// get the highlight rectangles for the touch point (x, y) with the slop
Vector<IntRect> WebViewCore::getTouchHighlightRects(int x, int y, int slop)
{
    Vector<IntRect> rects;
    m_mousePos = IntPoint(x - m_scrollOffsetX, y - m_scrollOffsetY);
    HitTestResult hitTestResult = m_mainFrame->eventHandler()->hitTestResultAtPoint(IntPoint(x, y),
            false, false, DontHitTestScrollbars, HitTestRequest::Active | HitTestRequest::ReadOnly, IntSize(slop, slop));
    if (!hitTestResult.innerNode() || !hitTestResult.innerNode()->inDocument()) {
        LOGE("Should not happen: no in document Node found");
        return rects;
    }
    const ListHashSet<RefPtr<Node> >& list = hitTestResult.rectBasedTestResult();
    if (list.isEmpty()) {
        LOGE("Should not happen: no rect-based-test nodes found");
        return rects;
    }
    Frame* frame = hitTestResult.innerNode()->document()->frame();
    Vector<TouchNodeData> nodeDataList;
    ListHashSet<RefPtr<Node> >::const_iterator last = list.end();
    for (ListHashSet<RefPtr<Node> >::const_iterator it = list.begin(); it != last; ++it) {
        // TODO: it seems reasonable to not search across the frame. Isn't it?
        // if the node is not in the same frame as the innerNode, skip it
        if (it->get()->document()->frame() != frame)
            continue;
        // traverse up the tree to find the first node that needs highlight
        bool found = false;
        Node* eventNode = it->get();
        while (eventNode) {
            RenderObject* render = eventNode->renderer();
            if (render->isBody() || render->isRenderView())
                break;
            if (eventNode->supportsFocus()
                    || eventNode->hasEventListeners(eventNames().clickEvent)
                    || eventNode->hasEventListeners(eventNames().mousedownEvent)
                    || eventNode->hasEventListeners(eventNames().mouseupEvent)) {
                found = true;
                break;
            }
            // the nodes in the rectBasedTestResult() are ordered based on z-index during hit testing.
            // so do not search for the eventNode across explicit z-index border.
            // TODO: this is a hard one to call. z-index is quite complicated as its value only
            // matters when you compare two RenderLayer in the same hierarchy level. e.g. in
            // the following example, "b" is on the top as its z level is the highest. even "c"
            // has 100 as z-index, it is still below "d" as its parent has the same z-index as
            // "d" and logically before "d". Of course "a" is the lowest in the z level.
            //
            // z-index:auto "a"
            //   z-index:2 "b"
            //   z-index:1
            //     z-index:100 "c"
            //   z-index:1 "d"
            //
            // If the fat point touches everyone, the order in the list should be "b", "d", "c"
            // and "a". When we search for the event node for "b", we really don't want "a" as
            // in the z-order it is behind everything else.
            if (!render->style()->hasAutoZIndex())
                break;
            eventNode = eventNode->parentNode();
        }
        // didn't find any eventNode, skip it
        if (!found)
            continue;
        // first quick check whether it is a duplicated node before computing bounding box
        Vector<TouchNodeData>::const_iterator nlast = nodeDataList.end();
        for (Vector<TouchNodeData>::const_iterator n = nodeDataList.begin(); n != nlast; ++n) {
            // found the same node, skip it
            if (eventNode == n->mNode) {
                found = false;
                break;
            }
        }
        if (!found)
            continue;
        // next check whether the node is fully covered by or fully covering another node.
        found = false;
        IntRect rect = getAbsoluteBoundingBox(eventNode);
        if (rect.isEmpty()) {
            // if the node's bounds is empty and it is not a ContainerNode, skip it.
            if (!eventNode->isContainerNode())
                continue;
            // if the node's children are all positioned objects, its bounds can be empty.
            // Walk through the children to find the bounding box.
            Node* child = static_cast<const ContainerNode*>(eventNode)->firstChild();
            while (child) {
                IntRect childrect;
                if (child->renderer())
                    childrect = getAbsoluteBoundingBox(child);
                if (!childrect.isEmpty()) {
                    rect.unite(childrect);
                    child = child->traverseNextSibling(eventNode);
                } else
                    child = child->traverseNextNode(eventNode);
            }
        }
        for (int i = nodeDataList.size() - 1; i >= 0; i--) {
            TouchNodeData n = nodeDataList.at(i);
            // the new node is enclosing an existing node, skip it
            if (rect.contains(n.mBounds)) {
                found = true;
                break;
            }
            // the new node is fully inside an existing node, remove the existing node
            if (n.mBounds.contains(rect))
                nodeDataList.remove(i);
        }
        if (!found) {
            TouchNodeData newNode;
            newNode.mNode = eventNode;
            newNode.mBounds = rect;
            nodeDataList.append(newNode);
        }
    }
    if (!nodeDataList.size())
        return rects;
    // finally select the node with the largest overlap with the fat point
    TouchNodeData final;
    final.mNode = 0;
    IntPoint docPos = frame->view()->windowToContents(m_mousePos);
    IntRect testRect(docPos.x() - slop, docPos.y() - slop, 2 * slop + 1, 2 * slop + 1);
    int area = 0;
    Vector<TouchNodeData>::const_iterator nlast = nodeDataList.end();
    for (Vector<TouchNodeData>::const_iterator n = nodeDataList.begin(); n != nlast; ++n) {
        IntRect rect = n->mBounds;
        rect.intersect(testRect);
        int a = rect.width() * rect.height();
        if (a > area) {
            final = *n;
            area = a;
        }
    }
    // now get the node's highlight rectangles in the page coordinate system
    if (final.mNode) {
        IntPoint frameAdjust;
        if (frame != m_mainFrame) {
            frameAdjust = frame->view()->contentsToWindow(IntPoint());
            frameAdjust.move(m_scrollOffsetX, m_scrollOffsetY);
        }
        if (final.mNode->isLink()) {
            // most of the links are inline instead of box style. So the bounding box is not
            // a good representation for the highlights. Get the list of rectangles instead.
            RenderObject* render = final.mNode->renderer();
            IntPoint offset = roundedIntPoint(render->localToAbsolute());
            render->absoluteRects(rects, offset.x() + frameAdjust.x(), offset.y() + frameAdjust.y());
            bool inside = false;
            int distance = INT_MAX;
            int newx = x, newy = y;
            int i = rects.size();
            while (i--) {
                if (rects[i].isEmpty()) {
                    rects.remove(i);
                    continue;
                }
                // check whether the point (x, y) is inside one of the rectangles.
                if (inside)
                    continue;
                if (rects[i].contains(x, y)) {
                    inside = true;
                    continue;
                }
                if (x >= rects[i].x() && x < rects[i].right()) {
                    if (y < rects[i].y()) {
                        if (rects[i].y() - y < distance) {
                            newx = x;
                            newy = rects[i].y();
                            distance = rects[i].y() - y;
                        }
                    } else if (y >= rects[i].bottom()) {
                        if (y - rects[i].bottom() + 1 < distance) {
                            newx = x;
                            newy = rects[i].bottom() - 1;
                            distance = y - rects[i].bottom() + 1;
                        }
                    }
                } else if (y >= rects[i].y() && y < rects[i].bottom()) {
                    if (x < rects[i].x()) {
                        if (rects[i].x() - x < distance) {
                            newx = rects[i].x();
                            newy = y;
                            distance = rects[i].x() - x;
                        }
                    } else if (x >= rects[i].right()) {
                        if (x - rects[i].right() + 1 < distance) {
                            newx = rects[i].right() - 1;
                            newy = y;
                            distance = x - rects[i].right() + 1;
                        }
                    }
                }
            }
            if (!rects.isEmpty()) {
                if (!inside) {
                    // if neither x nor y has overlap, just pick the top/left of the first rectangle
                    if (newx == x && newy == y) {
                        newx = rects[0].x();
                        newy = rects[0].y();
                    }
                    m_mousePos.setX(newx - m_scrollOffsetX);
                    m_mousePos.setY(newy - m_scrollOffsetY);
                    DBG_NAV_LOGD("Move x/y from (%d, %d) to (%d, %d) scrollOffset is (%d, %d)",
                            x, y, m_mousePos.x() + m_scrollOffsetX, m_mousePos.y() + m_scrollOffsetY,
                            m_scrollOffsetX, m_scrollOffsetY);
                }
                return rects;
            }
        }
        IntRect rect = final.mBounds;
        rect.move(frameAdjust.x(), frameAdjust.y());
        rects.append(rect);
        // adjust m_mousePos if it is not inside the returned highlight rectangle
        testRect.move(frameAdjust.x(), frameAdjust.y());
        testRect.intersect(rect);
        if (!testRect.contains(x, y)) {
            m_mousePos = testRect.center();
            m_mousePos.move(-m_scrollOffsetX, -m_scrollOffsetY);
            DBG_NAV_LOGD("Move x/y from (%d, %d) to (%d, %d) scrollOffset is (%d, %d)",
                    x, y, m_mousePos.x() + m_scrollOffsetX, m_mousePos.y() + m_scrollOffsetY,
                    m_scrollOffsetX, m_scrollOffsetY);
        }
    }
    return rects;
}

///////////////////////////////////////////////////////////////////////////////

void WebViewCore::addPlugin(PluginWidgetAndroid* w)
{
//    SkDebugf("----------- addPlugin %p", w);
    /* The plugin must be appended to the end of the array. This ensures that if
       the plugin is added while iterating through the array (e.g. sendEvent(...))
       that the iteration process is not corrupted.
     */
    *m_plugins.append() = w;
}

void WebViewCore::removePlugin(PluginWidgetAndroid* w)
{
//    SkDebugf("----------- removePlugin %p", w);
    int index = m_plugins.find(w);
    if (index < 0) {
        SkDebugf("--------------- pluginwindow not found! %p\n", w);
    } else {
        m_plugins.removeShuffle(index);
    }
}

bool WebViewCore::isPlugin(PluginWidgetAndroid* w) const
{
    return m_plugins.find(w) >= 0;
}

void WebViewCore::invalPlugin(PluginWidgetAndroid* w)
{
    const double PLUGIN_INVAL_DELAY = 1.0 / 60;

    if (!m_pluginInvalTimer.isActive()) {
        m_pluginInvalTimer.startOneShot(PLUGIN_INVAL_DELAY);
    }
}

void WebViewCore::drawPlugins()
{
    SkRegion inval; // accumualte what needs to be redrawn
    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();

    for (; iter < stop; ++iter) {
        PluginWidgetAndroid* w = *iter;
        SkIRect dirty;
        if (w->isDirty(&dirty)) {
            w->draw();
            inval.op(dirty, SkRegion::kUnion_Op);
        }
    }

    if (!inval.isEmpty()) {
        // inval.getBounds() is our rectangle
        const SkIRect& bounds = inval.getBounds();
        WebCore::IntRect r(bounds.fLeft, bounds.fTop,
                           bounds.width(), bounds.height());
        this->viewInvalidate(r);
    }
}

void WebViewCore::notifyPluginsOnFrameLoad(const Frame* frame) {
    // if frame is the parent then notify all plugins
    if (!frame->tree()->parent()) {
        // trigger an event notifying the plugins that the page has loaded
        ANPEvent event;
        SkANP::InitEvent(&event, kLifecycle_ANPEventType);
        event.data.lifecycle.action = kOnLoad_ANPLifecycleAction;
        sendPluginEvent(event);
    }
    // else if frame's parent has completed
    else if (!frame->tree()->parent()->loader()->isLoading()) {
        // send to all plugins who have this frame in their heirarchy
        PluginWidgetAndroid** iter = m_plugins.begin();
        PluginWidgetAndroid** stop = m_plugins.end();
        for (; iter < stop; ++iter) {
            Frame* currentFrame = (*iter)->pluginView()->parentFrame();
            while (currentFrame) {
                if (frame == currentFrame) {
                    ANPEvent event;
                    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
                    event.data.lifecycle.action = kOnLoad_ANPLifecycleAction;
                    (*iter)->sendEvent(event);
                    break;
                }
                currentFrame = currentFrame->tree()->parent();
            }
        }
    }
}

void WebViewCore::sendPluginVisibleScreen()
{
    /* We may want to cache the previous values and only send the notification
       to the plugin in the event that one of the values has changed.
     */

    ANPRectI visibleRect;
    visibleRect.left = m_scrollOffsetX;
    visibleRect.top = m_scrollOffsetY;
    visibleRect.right = m_scrollOffsetX + m_screenWidth;
    visibleRect.bottom = m_scrollOffsetY + m_screenHeight;

    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();
    for (; iter < stop; ++iter) {
        (*iter)->setVisibleScreen(visibleRect, m_scale);
    }
}

void WebViewCore::sendPluginEvent(const ANPEvent& evt)
{
    /* The list of plugins may be manipulated as we iterate through the list.
       This implementation allows for the addition of new plugins during an
       iteration, but may fail if a plugin is removed. Currently, there are not
       any use cases where a plugin is deleted while processing this loop, but
       if it does occur we will have to use an alternate data structure and/or
       iteration mechanism.
     */
    for (int x = 0; x < m_plugins.count(); x++) {
        m_plugins[x]->sendEvent(evt);
    }
}

PluginWidgetAndroid* WebViewCore::getPluginWidget(NPP npp)
{
    PluginWidgetAndroid** iter = m_plugins.begin();
    PluginWidgetAndroid** stop = m_plugins.end();
    for (; iter < stop; ++iter) {
        if ((*iter)->pluginView()->instance() == npp) {
            return (*iter);
        }
    }
    return NULL;
}

static PluginView* nodeIsPlugin(Node* node) {
    RenderObject* renderer = node->renderer();
    if (renderer && renderer->isWidget()) {
        Widget* widget = static_cast<RenderWidget*>(renderer)->widget();
        if (widget && widget->isPluginView())
            return static_cast<PluginView*>(widget);
    }
    return 0;
}

Node* WebViewCore::cursorNodeIsPlugin() {
    gCursorBoundsMutex.lock();
    bool hasCursorBounds = m_hasCursorBounds;
    Frame* frame = (Frame*) m_cursorFrame;
    Node* node = (Node*) m_cursorNode;
    gCursorBoundsMutex.unlock();
    if (hasCursorBounds && CacheBuilder::validNode(m_mainFrame, frame, node)
            && nodeIsPlugin(node)) {
        return node;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
void WebViewCore::moveMouseIfLatest(int moveGeneration,
    WebCore::Frame* frame, int x, int y)
{
    DBG_NAV_LOGD("m_moveGeneration=%d moveGeneration=%d"
        " frame=%p x=%d y=%d",
        m_moveGeneration, moveGeneration, frame, x, y);
    if (m_moveGeneration > moveGeneration) {
        DBG_NAV_LOGD("m_moveGeneration=%d > moveGeneration=%d",
            m_moveGeneration, moveGeneration);
        return; // short-circuit if a newer move has already been generated
    }
    m_lastGeneration = moveGeneration;
    moveMouse(frame, x, y);
}

void WebViewCore::moveFocus(WebCore::Frame* frame, WebCore::Node* node)
{
    DBG_NAV_LOGD("frame=%p node=%p", frame, node);
    if (!node || !CacheBuilder::validNode(m_mainFrame, frame, node)
            || !node->isElementNode())
        return;
    // Code borrowed from FocusController::advanceFocus
    WebCore::FocusController* focusController
            = m_mainFrame->page()->focusController();
    WebCore::Document* oldDoc
            = focusController->focusedOrMainFrame()->document();
    if (oldDoc->focusedNode() == node)
        return;
    if (node->document() != oldDoc)
        oldDoc->setFocusedNode(0);
    focusController->setFocusedFrame(frame);
    static_cast<WebCore::Element*>(node)->focus(false);
}

// Update mouse position
void WebViewCore::moveMouse(WebCore::Frame* frame, int x, int y)
{
    DBG_NAV_LOGD("frame=%p x=%d y=%d scrollOffset=(%d,%d)", frame,
        x, y, m_scrollOffsetX, m_scrollOffsetY);
    if (!frame || CacheBuilder::validNode(m_mainFrame, frame, NULL) == false)
        frame = m_mainFrame;
    // mouse event expects the position in the window coordinate
    m_mousePos = WebCore::IntPoint(x - m_scrollOffsetX, y - m_scrollOffsetY);
    // validNode will still return true if the node is null, as long as we have
    // a valid frame.  Do not want to make a call on frame unless it is valid.
    WebCore::PlatformMouseEvent mouseEvent(m_mousePos, m_mousePos,
        WebCore::NoButton, WebCore::MouseEventMoved, 1, false, false, false,
        false, WTF::currentTime());
    frame->eventHandler()->handleMouseMoveEvent(mouseEvent);
    updateCacheOnNodeChange();
}

void WebViewCore::setSelection(int start, int end)
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    WebCore::RenderObject* renderer = focus->renderer();
    if (!renderer || (!renderer->isTextField() && !renderer->isTextArea()))
        return;
    WebCore::RenderTextControl* rtc = static_cast<WebCore::RenderTextControl*>(renderer);
    if (start > end) {
        int temp = start;
        start = end;
        end = temp;
    }
    // Tell our EditorClient that this change was generated from the UI, so it
    // does not need to echo it to the UI.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    rtc->setSelectionRange(start, end);
    client->setUiGeneratedSelectionChange(false);
    WebCore::Frame* focusedFrame = focus->document()->frame();
    if (renderer->isTextArea()
            // For password fields, this is done in the UI side via
            // bringPointIntoView, since the UI does the drawing.
            || !static_cast<WebCore::HTMLInputElement*>(focus)->isPasswordField()) {
        WebFrame* webFrame = WebFrame::getWebFrame(focusedFrame);
        webFrame->setUserInitiatedAction(true);
        focusedFrame->selection()->revealSelection();
        webFrame->setUserInitiatedAction(false);
    }
}

String WebViewCore::modifySelection(const int direction, const int axis)
{
    DOMSelection* selection = m_mainFrame->domWindow()->getSelection();
    if (selection->rangeCount() > 1)
        selection->removeAllRanges();
    switch (axis) {
    case AXIS_CHARACTER:
    case AXIS_WORD:
    case AXIS_SENTENCE:
        return modifySelectionTextNavigationAxis(selection, direction, axis);
    case AXIS_HEADING:
    case AXIS_SIBLING:
    case AXIS_PARENT_FIRST_CHILD:
    case AXIS_DOCUMENT:
        return modifySelectionDomNavigationAxis(selection, direction, axis);
    default:
        LOGE("Invalid navigation axis: %d", axis);
        return String();
    }
}

String WebViewCore::modifySelectionTextNavigationAxis(DOMSelection* selection, int direction, int axis)
{
    String directionString;
    if (direction == DIRECTION_FORWARD)
        directionString = "forward";
    else if (direction == DIRECTION_BACKWARD)
        directionString = "backward";
    else {
        LOGE("Invalid direction: %d", direction);
        return String();
    }
    String axisString;
    if (axis == AXIS_CHARACTER)
        axisString = "character";
    else if (axis == AXIS_WORD)
        axisString = "word";
    else // axis == AXIS_SENTENCE
        axisString = "sentence";

    // TODO: Add support of IFrames.
    HTMLElement* body = m_mainFrame->document()->body();

    Node* focusNode = 0;
    if (m_currentNodeDomNavigationAxis
            && CacheBuilder::validNode(m_mainFrame, m_mainFrame, m_currentNodeDomNavigationAxis)) {
        focusNode = m_currentNodeDomNavigationAxis;
        m_currentNodeDomNavigationAxis = 0;
        do {
            focusNode = (direction == DIRECTION_FORWARD) ?
                    focusNode->traverseNextNode(body) :
                    focusNode->traversePreviousNode(body);
        } while (focusNode && focusNode->isTextNode());
    } else
        focusNode = (selection->focusNode()) ? selection->focusNode() : currentFocus();

    Text* currentNode = 0;
    if (!focusNode) {
        // we have no selection so start from the body or its recursively last child
        focusNode = (direction == DIRECTION_FORWARD) ? body : body->lastDescendant();
        if (focusNode->isTextNode())
            currentNode = static_cast<Text*>(focusNode);
        else
            currentNode = traverseNonEmptyNonWhitespaceTextNode(focusNode, body, direction);
        if (!setSelection(selection, currentNode, direction))
            return String();
    } else if (focusNode->isElementNode()) {
        // find a non-empty text node in the current direction
        currentNode = traverseNonEmptyNonWhitespaceTextNode(focusNode, body, direction);
        if (!setSelection(selection, currentNode, direction))
            return String();
    } else {
        currentNode = static_cast<Text*>(focusNode);
        if (direction == DIRECTION_FORWARD) {
            // if end of non-whitespace text go to the next non-empty text node
            int higherIndex = (selection->focusOffset() > selection->anchorOffset()) ?
                    selection->focusOffset() : selection->anchorOffset();
            String suffix = currentNode->data().substring(higherIndex, currentNode->length());
            if (suffix.isEmpty() || suffix.stripWhiteSpace().isEmpty()) {
                currentNode = traverseNonEmptyNonWhitespaceTextNode(currentNode, body, direction);
                if (!setSelection(selection, currentNode, direction))
                    return String();
            } else {
              ExceptionCode ec = 0;
                selection->collapseToEnd(ec);
                if (ec)
                    LOGE("Error while collapsing selection. Error code: %d", ec);
            }
        } else {
            // if beginning of non-whitespace text go to the previous non-empty text node
            int lowerIndex = (selection->focusOffset() > selection->anchorOffset()) ?
                    selection->anchorOffset() : selection->focusOffset();
            String prefix = currentNode->data().substring(0, lowerIndex);
            if (prefix.isEmpty() || prefix.stripWhiteSpace().isEmpty()) {
                currentNode = traverseNonEmptyNonWhitespaceTextNode(currentNode, body, direction);
                if (!setSelection(selection, currentNode, direction))
                    return String();
            } else {
                ExceptionCode ec = 0;
                selection->collapseToStart(ec);
                if (ec)
                    LOGE("Error while collapsing selection. Error code: %d", ec);
            }
        }
    }

    // extend the selection - loop as an insurance it does not get stuck
    currentNode = static_cast<Text*>(selection->focusNode());
    int focusOffset = selection->focusOffset();
    while (true) {
        selection->modify("extend", directionString, axisString);
        if (selection->focusNode() != currentNode || selection->focusOffset() != focusOffset)
            break;
        currentNode = traverseNonEmptyNonWhitespaceTextNode(currentNode, body, direction);
        focusOffset = (direction == DIRECTION_FORWARD) ? 0 : currentNode->data().length();
        // setSelection returns false if currentNode is 0 => the loop always terminates
        if (!setSelection(selection, currentNode, direction))
            return String();
    }

    if (direction == DIRECTION_FORWARD) {
        // enforce the anchor node is a text node
        if (selection->anchorNode()->isElementNode()) {
            if (!setSelection(selection, selection->focusNode(), selection->focusNode(), 0,
                              selection->focusOffset()))
                return String();
        }
        // enforce the focus node is a text node
        if (selection->focusNode()->isElementNode()) {
            int endOffset = static_cast<Text*>(selection->anchorNode())->length(); // cast is safe
            if (!setSelection(selection, selection->anchorNode(), selection->anchorNode(),
                              selection->anchorOffset(), endOffset))
                return String();
        }
    } else {
        // enforce the focus node is a text node
        if (selection->focusNode()->isElementNode()) {
            if (!setSelection(selection, selection->anchorNode(), selection->anchorNode(), 0,
                              selection->anchorOffset()))
                return String();
        }
        // enforce the anchor node is a text node
        if (selection->anchorNode()->isElementNode()) {
            int endOffset = static_cast<Text*>(selection->focusNode())->length(); // cast is safe
            if (!setSelection(selection, selection->focusNode(), selection->focusNode(),
                              selection->focusOffset(), endOffset))
                return String();
        }
    }

    tryFocusInlineSelectionElement(selection);
    // TODO (svetoslavganov): Draw the selected text in the WebView - a-la-Android
    String markup = formatMarkup(selection).stripWhiteSpace();
    LOGD("Selection markup: %s", markup.utf8().data());
    return markup;
}

bool WebViewCore::setSelection(DOMSelection* selection, Text* textNode, int direction)
{
    if (!textNode)
        return false;
    int offset = (direction == DIRECTION_FORWARD) ? 0 : textNode->length();
    if (!setSelection(selection, textNode, textNode, offset, offset))
        return false;
    return true;
}

bool WebViewCore::setSelection(DOMSelection* selection, Node* startNode, Node* endNode, int startOffset, int endOffset)
{
    if (!selection || (!startNode && !endNode))
        return false;
    ExceptionCode ec = 0;
    PassRefPtr<Range> rangeRef = selection->getRangeAt(0, ec);
    if (ec) {
        ec = 0;
        rangeRef = m_mainFrame->document()->createRange();
    }
    if (startNode)
        rangeRef->setStart(PassRefPtr<Node>(startNode), startOffset, ec);
    if (ec)
        return false;
    if (endNode)
        rangeRef->setEnd(PassRefPtr<Node>(endNode), endOffset, ec);
    if (ec)
        return false;
    selection->removeAllRanges();
    selection->addRange(rangeRef.get());
    return true;
}

Text* WebViewCore::traverseNonEmptyNonWhitespaceTextNode(Node* fromNode, Node* toNode, int direction)
{
    Node* currentNode = fromNode;
    do {
        if (direction == DIRECTION_FORWARD)
            currentNode = currentNode->traverseNextNode(toNode);
        else
            currentNode = currentNode->traversePreviousNode(toNode);
    } while (currentNode && (!currentNode->isTextNode()
            || isEmptyOrOnlyWhitespaceTextNode(currentNode)));
    return static_cast<Text*>(currentNode);
}

bool WebViewCore::isEmptyOrOnlyWhitespaceTextNode(Node* node)
{
    return (node->isTextNode()
          && (static_cast<Text*>(node)->length() == 0
          || static_cast<Text*>(node)->containsOnlyWhitespace()));
}

String WebViewCore::modifySelectionDomNavigationAxis(DOMSelection* selection, int direction, int axis)
{
    // TODO: Add support of IFrames.
    HTMLElement* body = m_mainFrame->document()->body();
    if (!m_currentNodeDomNavigationAxis && selection->focusNode()) {
        m_currentNodeDomNavigationAxis = selection->focusNode();
        selection->empty();
        if (m_currentNodeDomNavigationAxis->isTextNode())
            m_currentNodeDomNavigationAxis = m_currentNodeDomNavigationAxis->parentNode();
    }
    if (!m_currentNodeDomNavigationAxis)
        m_currentNodeDomNavigationAxis = currentFocus();
    if (!m_currentNodeDomNavigationAxis
            || !CacheBuilder::validNode(m_mainFrame, m_mainFrame, m_currentNodeDomNavigationAxis))
        m_currentNodeDomNavigationAxis = body;
    Node* currentNode = m_currentNodeDomNavigationAxis;
    if (axis == AXIS_HEADING) {
        if (currentNode == body && direction == DIRECTION_BACKWARD)
            currentNode = currentNode->lastDescendant();
        do {
            if (direction == DIRECTION_FORWARD)
                currentNode = currentNode->traverseNextNode(body);
            else
                currentNode = currentNode->traversePreviousNode(body);
        } while (currentNode && (currentNode->isTextNode() || !isVisible(currentNode)
                || !isHeading(currentNode)));
    } else if (axis == AXIS_PARENT_FIRST_CHILD) {
        if (direction == DIRECTION_FORWARD) {
            currentNode = currentNode->firstChild();
            while (currentNode && (currentNode->isTextNode() || !isVisible(currentNode)))
                currentNode = currentNode->nextSibling();
        } else {
            do {
                if (currentNode == body)
                    return String();
                currentNode = currentNode->parentNode();
            } while (currentNode && (currentNode->isTextNode() || !isVisible(currentNode)));
        }
    } else if (axis == AXIS_SIBLING) {
        do {
            if (direction == DIRECTION_FORWARD)
                currentNode = currentNode->nextSibling();
            else {
                if (currentNode == body)
                    return String();
                currentNode = currentNode->previousSibling();
            }
        } while (currentNode && (currentNode->isTextNode() || !isVisible(currentNode)));
    } else if (axis == AXIS_DOCUMENT) {
        currentNode = body;
        if (direction == DIRECTION_FORWARD)
            currentNode = currentNode->lastDescendant();
    } else {
        LOGE("Invalid axis: %d", axis);
        return String();
    }
    if (currentNode) {
        m_currentNodeDomNavigationAxis = currentNode;
        focusIfFocusableAndNotTextInput(selection, currentNode);
        // TODO (svetoslavganov): Draw the selected text in the WebView - a-la-Android
        String selectionString = createMarkup(currentNode);
        LOGD("Selection markup: %s", selectionString.utf8().data());
        return selectionString;
    }
    return String();
}

bool WebViewCore::isHeading(Node* node)
{
    if (node->hasTagName(WebCore::HTMLNames::h1Tag)
            || node->hasTagName(WebCore::HTMLNames::h2Tag)
            || node->hasTagName(WebCore::HTMLNames::h3Tag)
            || node->hasTagName(WebCore::HTMLNames::h4Tag)
            || node->hasTagName(WebCore::HTMLNames::h5Tag)
            || node->hasTagName(WebCore::HTMLNames::h6Tag)) {
        return true;
    }

    if (node->isElementNode()) {
        Element* element = static_cast<Element*>(node);
        String roleAttribute = element->getAttribute(WebCore::HTMLNames::roleAttr).string();
        if (equalIgnoringCase(roleAttribute, "heading"))
            return true;
    }

    return false;
}

bool WebViewCore::isVisible(Node* node)
{
    if (!node->isStyledElement())
        return false;
    RenderStyle* style = node->computedStyle();
    return (style->display() != NONE && style->visibility() != HIDDEN);
}

String WebViewCore::formatMarkup(DOMSelection* selection)
{
    ExceptionCode ec = 0;
    PassRefPtr<Range> rangeRef = selection->getRangeAt(0, ec);
    if (ec) {
        LOGE("Error accessing the first selection range. Error code: %d", ec);
        return String();
    }
    // TODO: This breaks in certain cases - WebKit bug. Figure out and work around it
    String markup = createMarkup(rangeRef.get());
    int fromIdx = markup.find("<span class=\"Apple-style-span\"");
    while (fromIdx > -1) {
        unsigned toIdx = markup.find(">");
        markup = markup.replace(fromIdx, toIdx - fromIdx + 1, "");
        markup = markup.replace("</span>", "");
        fromIdx = markup.find("<span class=\"Apple-style-span\"");
    }
    return markup;
}

void WebViewCore::tryFocusInlineSelectionElement(DOMSelection* selection)
{
    Node* currentNode = selection->anchorNode();
    Node* endNode = selection->focusNode();
    while (currentNode) {
        if (focusIfFocusableAndNotTextInput(selection, currentNode))
            return;
        currentNode = currentNode->traverseNextNode(endNode);
    }
}

bool WebViewCore::focusIfFocusableAndNotTextInput(DOMSelection* selection, Node* node)
{
    // TODO (svetoslavganov): Synchronize Android and WebKit focus
    if (node->isFocusable()) {
        WebCore::RenderObject* renderer = node->renderer();
        if (!renderer || (!renderer->isTextField() && !renderer->isTextArea())) {
            // restore the selection after focus workaround for
            // the FIXME in Element.cpp#updateFocusAppearance
            ExceptionCode ec = 0;
            PassRefPtr<Range> rangeRef = selection->getRangeAt(0, ec);
            moveFocus(m_mainFrame, node);
            if (rangeRef)
                selection->addRange(rangeRef.get());
            return true;
        }
    }
    return false;
}

void WebViewCore::deleteSelection(int start, int end, int textGeneration)
{
    setSelection(start, end);
    if (start == end)
        return;
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    // Prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    PlatformKeyboardEvent down(AKEYCODE_DEL, 0, 0, true, false, false, false);
    PlatformKeyboardEvent up(AKEYCODE_DEL, 0, 0, false, false, false, false);
    key(down);
    key(up);
    client->setUiGeneratedSelectionChange(false);
    m_textGeneration = textGeneration;
}

void WebViewCore::replaceTextfieldText(int oldStart,
        int oldEnd, const WTF::String& replace, int start, int end,
        int textGeneration)
{
    WebCore::Node* focus = currentFocus();
    if (!focus)
        return;
    setSelection(oldStart, oldEnd);
    // Prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    WebCore::TypingCommand::insertText(focus->document(), replace,
        false);
    client->setUiGeneratedSelectionChange(false);
    // setSelection calls revealSelection, so there is no need to do it here.
    setSelection(start, end);
    m_textGeneration = textGeneration;
}

void WebViewCore::passToJs(int generation, const WTF::String& current,
    const PlatformKeyboardEvent& event)
{
    WebCore::Node* focus = currentFocus();
    if (!focus) {
        DBG_NAV_LOG("!focus");
        clearTextEntry();
        return;
    }
    WebCore::RenderObject* renderer = focus->renderer();
    if (!renderer || (!renderer->isTextField() && !renderer->isTextArea())) {
        DBG_NAV_LOGD("renderer==%p || not text", renderer);
        clearTextEntry();
        return;
    }
    // Block text field updates during a key press.
    m_blockTextfieldUpdates = true;
    // Also prevent our editor client from passing a message to change the
    // selection.
    EditorClientAndroid* client = static_cast<EditorClientAndroid*>(
            m_mainFrame->editor()->client());
    client->setUiGeneratedSelectionChange(true);
    key(event);
    client->setUiGeneratedSelectionChange(false);
    m_blockTextfieldUpdates = false;
    m_textGeneration = generation;
    WebCore::RenderTextControl* renderText =
        static_cast<WebCore::RenderTextControl*>(renderer);
    WTF::String test = renderText->text();
    if (test == current) {
        DBG_NAV_LOG("test == current");
        return;
    }
    // If the text changed during the key event, update the UI text field.
    updateTextfield(focus, false, test);
}

void WebViewCore::scrollFocusedTextInput(float xPercent, int y)
{
    WebCore::Node* focus = currentFocus();
    if (!focus) {
        DBG_NAV_LOG("!focus");
        clearTextEntry();
        return;
    }
    WebCore::RenderObject* renderer = focus->renderer();
    if (!renderer || (!renderer->isTextField() && !renderer->isTextArea())) {
        DBG_NAV_LOGD("renderer==%p || not text", renderer);
        clearTextEntry();
        return;
    }
    WebCore::RenderTextControl* renderText =
        static_cast<WebCore::RenderTextControl*>(renderer);
    int x = (int) (xPercent * (renderText->scrollWidth() -
        renderText->clientWidth()));
    DBG_NAV_LOGD("x=%d y=%d xPercent=%g scrollW=%d clientW=%d", x, y,
        xPercent, renderText->scrollWidth(), renderText->clientWidth());
    renderText->setScrollLeft(x);
    renderText->setScrollTop(y);
}

void WebViewCore::setFocusControllerActive(bool active)
{
    m_mainFrame->page()->focusController()->setActive(active);
}

void WebViewCore::saveDocumentState(WebCore::Frame* frame)
{
    if (!CacheBuilder::validNode(m_mainFrame, frame, 0))
        frame = m_mainFrame;
    WebCore::HistoryItem *item = frame->loader()->history()->currentItem();

    // item can be null when there is no offical URL for the current page. This happens
    // when the content is loaded using with WebCoreFrameBridge::LoadData() and there
    // is no failing URL (common case is when content is loaded using data: scheme)
    if (item) {
        item->setDocumentState(frame->document()->formElementsState());
    }
}

// Convert a WTF::String into an array of characters where the first
// character represents the length, for easy conversion to java.
static uint16_t* stringConverter(const WTF::String& text)
{
    size_t length = text.length();
    uint16_t* itemName = new uint16_t[length+1];
    itemName[0] = (uint16_t)length;
    uint16_t* firstChar = &(itemName[1]);
    memcpy((void*)firstChar, text.characters(), sizeof(UChar)*length);
    return itemName;
}

// Response to dropdown created for a listbox.
class ListBoxReply : public WebCoreReply {
public:
    ListBoxReply(WebCore::HTMLSelectElement* select, WebCore::Frame* frame, WebViewCore* view)
        : m_select(select)
        , m_frame(frame)
        , m_viewImpl(view)
    {}

    // Response used if the listbox only allows single selection.
    // index is listIndex of the selected item, or -1 if nothing is selected.
    virtual void replyInt(int index)
    {
        if (-2 == index) {
            // Special value for cancel. Do nothing.
            return;
        }
        // If the select element no longer exists, due to a page change, etc,
        // silently return.
        if (!m_select || !CacheBuilder::validNode(m_viewImpl->m_mainFrame,
                m_frame, m_select))
            return;
        // Use a pointer to HTMLSelectElement's superclass, where
        // listToOptionIndex is public.
        SelectElement* selectElement = m_select;
        int optionIndex = selectElement->listToOptionIndex(index);
        m_select->setSelectedIndex(optionIndex, true);
        m_select->dispatchFormControlChangeEvent();
        m_viewImpl->contentInvalidate(m_select->getRect());
    }

    // Response if the listbox allows multiple selection.  array stores the listIndices
    // of selected positions.
    virtual void replyIntArray(const int* array, int count)
    {
        // If the select element no longer exists, due to a page change, etc,
        // silently return.
        if (!m_select || !CacheBuilder::validNode(m_viewImpl->m_mainFrame,
                m_frame, m_select))
            return;

        // If count is 1 or 0, use replyInt.
        SkASSERT(count > 1);

        const WTF::Vector<Element*>& items = m_select->listItems();
        int totalItems = static_cast<int>(items.size());
        // Keep track of the position of the value we are comparing against.
        int arrayIndex = 0;
        // The value we are comparing against.
        int selection = array[arrayIndex];
        WebCore::HTMLOptionElement* option;
        for (int listIndex = 0; listIndex < totalItems; listIndex++) {
            if (items[listIndex]->hasLocalName(WebCore::HTMLNames::optionTag)) {
                option = static_cast<WebCore::HTMLOptionElement*>(
                        items[listIndex]);
                if (listIndex == selection) {
                    option->setSelectedState(true);
                    arrayIndex++;
                    if (arrayIndex == count)
                        selection = -1;
                    else
                        selection = array[arrayIndex];
                } else
                    option->setSelectedState(false);
            }
        }
        m_select->dispatchFormControlChangeEvent();
        m_viewImpl->contentInvalidate(m_select->getRect());
    }
private:
    // The select element associated with this listbox.
    WebCore::HTMLSelectElement* m_select;
    // The frame of this select element, to verify that it is valid.
    WebCore::Frame* m_frame;
    // For calling invalidate and checking the select element's validity
    WebViewCore* m_viewImpl;
};

// Create an array of java Strings.
static jobjectArray makeLabelArray(JNIEnv* env, const uint16_t** labels, size_t count)
{
    jclass stringClass = env->FindClass("java/lang/String");
    LOG_ASSERT(stringClass, "Could not find java/lang/String");
    jobjectArray array = env->NewObjectArray(count, stringClass, 0);
    LOG_ASSERT(array, "Could not create new string array");

    for (size_t i = 0; i < count; i++) {
        jobject newString = env->NewString(&labels[i][1], labels[i][0]);
        env->SetObjectArrayElement(array, i, newString);
        env->DeleteLocalRef(newString);
        checkException(env);
    }
    env->DeleteLocalRef(stringClass);
    return array;
}

void WebViewCore::openFileChooser(PassRefPtr<WebCore::FileChooser> chooser) {
    if (!chooser)
        return;
    JNIEnv* env = JSC::Bindings::getJNIEnv();

    WTF::String acceptType = chooser->acceptTypes();
    jstring jAcceptType = env->NewString(const_cast<unsigned short*>(acceptType.characters()), acceptType.length());
    jstring jName = (jstring) env->CallObjectMethod(
            m_javaGlue->object(env).get(), m_javaGlue->m_openFileChooser, jAcceptType);
    checkException(env);
    env->DeleteLocalRef(jAcceptType);

    const UChar* string = static_cast<const UChar*>(env->GetStringChars(jName, NULL));

    if (!string)
        return;

    WTF::String webcoreString = to_string(env, jName);
    env->ReleaseStringChars(jName, string);

    if (webcoreString.length())
        chooser->chooseFile(webcoreString);
}

void WebViewCore::listBoxRequest(WebCoreReply* reply, const uint16_t** labels, size_t count, const int enabled[], size_t enabledCount,
        bool multiple, const int selected[], size_t selectedCountOrSelection)
{
    // If m_popupReply is not null, then we already have a list showing.
    if (m_popupReply != 0)
        return;

    LOG_ASSERT(m_javaGlue->m_obj, "No java widget associated with this view!");

    // Create an array of java Strings for the drop down.
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jobjectArray labelArray = makeLabelArray(env, labels, count);

    // Create an array determining whether each item is enabled.
    jintArray enabledArray = env->NewIntArray(enabledCount);
    checkException(env);
    jint* ptrArray = env->GetIntArrayElements(enabledArray, 0);
    checkException(env);
    for (size_t i = 0; i < enabledCount; i++) {
        ptrArray[i] = enabled[i];
    }
    env->ReleaseIntArrayElements(enabledArray, ptrArray, 0);
    checkException(env);

    if (multiple) {
        // Pass up an array representing which items are selected.
        jintArray selectedArray = env->NewIntArray(selectedCountOrSelection);
        checkException(env);
        jint* selArray = env->GetIntArrayElements(selectedArray, 0);
        checkException(env);
        for (size_t i = 0; i < selectedCountOrSelection; i++) {
            selArray[i] = selected[i];
        }
        env->ReleaseIntArrayElements(selectedArray, selArray, 0);

        env->CallVoidMethod(m_javaGlue->object(env).get(),
                m_javaGlue->m_requestListBox, labelArray, enabledArray,
                selectedArray);
        env->DeleteLocalRef(selectedArray);
    } else {
        // Pass up the single selection.
        env->CallVoidMethod(m_javaGlue->object(env).get(),
                m_javaGlue->m_requestSingleListBox, labelArray, enabledArray,
                selectedCountOrSelection);
    }

    env->DeleteLocalRef(labelArray);
    env->DeleteLocalRef(enabledArray);
    checkException(env);

    Retain(reply);
    m_popupReply = reply;
}

bool WebViewCore::key(const PlatformKeyboardEvent& event)
{
    WebCore::EventHandler* eventHandler;
    WebCore::Node* focusNode = currentFocus();
    DBG_NAV_LOGD("keyCode=%s unichar=%d focusNode=%p",
        event.keyIdentifier().utf8().data(), event.unichar(), focusNode);
    if (focusNode) {
        WebCore::Frame* frame = focusNode->document()->frame();
        WebFrame* webFrame = WebFrame::getWebFrame(frame);
        if (focusNode->isContentEditable() || (focusNode->renderer()
                && (focusNode->renderer()->isTextArea()
                // For password fields, this is done in the UI side via
                // bringPointIntoView, since the UI does the drawing.
                || !static_cast<WebCore:: HTMLInputElement*>(focusNode)->isPasswordField()))) {
            webFrame->setUserInitiatedAction(true);
        }
        eventHandler = frame->eventHandler();
        VisibleSelection old = frame->selection()->selection();
        bool handled = eventHandler->keyEvent(event);
        webFrame->setUserInitiatedAction(false);
        if (focusNode->isContentEditable()) {
            // keyEvent will return true even if the contentEditable did not
            // change its selection.  In the case that it does not, we want to
            // return false so that the key will be sent back to our navigation
            // system.
            handled = frame->selection()->selection() != old;
        }
        return handled;
    } else {
        eventHandler = m_mainFrame->eventHandler();
    }
    return eventHandler->keyEvent(event);
}

// For when the user clicks the trackball
void WebViewCore::click(WebCore::Frame* frame, WebCore::Node* node) {
    if (!node) {
        WebCore::IntPoint pt = m_mousePos;
        pt.move(m_scrollOffsetX, m_scrollOffsetY);
        WebCore::HitTestResult hitTestResult = m_mainFrame->eventHandler()->
                hitTestResultAtPoint(pt, false);
        node = hitTestResult.innerNode();
        frame = node->document()->frame();
        DBG_NAV_LOGD("m_mousePos=(%d,%d) m_scrollOffset=(%d,%d) pt=(%d,%d)"
            " node=%p", m_mousePos.x(), m_mousePos.y(),
            m_scrollOffsetX, m_scrollOffsetY, pt.x(), pt.y(), node);
    }
    if (node) {
        EditorClientAndroid* client
                = static_cast<EditorClientAndroid*>(
                m_mainFrame->editor()->client());
        client->setShouldChangeSelectedRange(false);
        handleMouseClick(frame, node);
        client->setShouldChangeSelectedRange(true);
    }
}

#if USE(ACCELERATED_COMPOSITING)
GraphicsLayerAndroid* WebViewCore::graphicsRootLayer() const
{
    RenderView* contentRenderer = m_mainFrame->contentRenderer();
    if (!contentRenderer)
        return 0;
    return static_cast<GraphicsLayerAndroid*>(
          contentRenderer->compositor()->rootPlatformLayer());
}
#endif

bool WebViewCore::handleTouchEvent(int action, Vector<IntPoint>& points, int metaState)
{
    bool preventDefault = false;

#if USE(ACCELERATED_COMPOSITING)
    GraphicsLayerAndroid* rootLayer = graphicsRootLayer();
    if (rootLayer)
      rootLayer->pauseDisplay(true);
#endif

#if ENABLE(TOUCH_EVENTS) // Android
    WebCore::TouchEventType type = WebCore::TouchStart;
    WebCore::PlatformTouchPoint::State touchState = WebCore::PlatformTouchPoint::TouchPressed;
    switch (action) {
    case 0: // MotionEvent.ACTION_DOWN
    case 5: // MotionEvent.ACTION_POINTER_DOWN
        type = WebCore::TouchStart;
        break;
    case 1: // MotionEvent.ACTION_UP
    case 6: // MotionEvent.ACTION_POINTER_UP
        type = WebCore::TouchEnd;
        touchState = WebCore::PlatformTouchPoint::TouchReleased;
        break;
    case 2: // MotionEvent.ACTION_MOVE
        type = WebCore::TouchMove;
        touchState = WebCore::PlatformTouchPoint::TouchMoved;
        break;
    case 3: // MotionEvent.ACTION_CANCEL
        type = WebCore::TouchCancel;
        touchState = WebCore::PlatformTouchPoint::TouchCancelled;
        break;
    case 0x100: // WebViewCore.ACTION_LONGPRESS
        type = WebCore::TouchLongPress;
        touchState = WebCore::PlatformTouchPoint::TouchPressed;
        break;
    case 0x200: // WebViewCore.ACTION_DOUBLETAP
        type = WebCore::TouchDoubleTap;
        touchState = WebCore::PlatformTouchPoint::TouchPressed;
        break;
    default:
        // We do not support other kinds of touch event inside WebCore
        // at the moment.
        LOGW("Java passed a touch event type that we do not support in WebCore: %d", action);
        return 0;
    }

    // Track previous touch and if stationary set the state.
    for (unsigned c = 0; c < points.size(); c++) {
        points[c].setX(points[c].x() - m_scrollOffsetX);
        points[c].setY(points[c].y() - m_scrollOffsetY);
    }
    WebCore::PlatformTouchEvent te(points, type, touchState, metaState);
    preventDefault = m_mainFrame->eventHandler()->handleTouchEvent(te);
#endif

#if USE(ACCELERATED_COMPOSITING)
    if (rootLayer)
      rootLayer->pauseDisplay(false);
#endif
    return preventDefault;
}

void WebViewCore::touchUp(int touchGeneration,
    WebCore::Frame* frame, WebCore::Node* node, int x, int y)
{
    if (touchGeneration == 0) {
        // m_mousePos should be set in getTouchHighlightRects()
        WebCore::HitTestResult hitTestResult = m_mainFrame->eventHandler()->hitTestResultAtPoint(m_mousePos, false);
        node = hitTestResult.innerNode();
        if (node)
            frame = node->document()->frame();
        else
            frame = 0;
        DBG_NAV_LOGD("touch up on (%d, %d), scrollOffset is (%d, %d), node:%p, frame:%p", m_mousePos.x() + m_scrollOffsetX, m_mousePos.y() + m_scrollOffsetY, m_scrollOffsetX, m_scrollOffsetY, node, frame);
    } else {
        if (m_touchGeneration > touchGeneration) {
            DBG_NAV_LOGD("m_touchGeneration=%d > touchGeneration=%d"
                " x=%d y=%d", m_touchGeneration, touchGeneration, x, y);
            return; // short circuit if a newer touch has been generated
        }
        // This moves m_mousePos to the correct place, and handleMouseClick uses
        // m_mousePos to determine where the click happens.
        moveMouse(frame, x, y);
        m_lastGeneration = touchGeneration;
    }
    if (frame && CacheBuilder::validNode(m_mainFrame, frame, 0)) {
        frame->loader()->resetMultipleFormSubmissionProtection();
    }
    DBG_NAV_LOGD("touchGeneration=%d handleMouseClick frame=%p node=%p"
        " x=%d y=%d", touchGeneration, frame, node, x, y);
    handleMouseClick(frame, node);
}

// Return the RenderLayer for the given RenderObject only if the layer is
// composited and it contains a scrollable content layer.
static WebCore::RenderLayer* getLayerFromRenderer(
        WebCore::RenderObject* renderer, LayerAndroid** aLayer)
{
    if (!renderer)
        return 0;
    WebCore::RenderLayer* layer = renderer->enclosingSelfPaintingLayer();
    if (!layer || !layer->backing())
        return 0;
    GraphicsLayerAndroid* graphicsLayer =
            static_cast<GraphicsLayerAndroid*>(layer->backing()->graphicsLayer());
    if (!graphicsLayer)
        return 0;
    LayerAndroid* layerAndroid = graphicsLayer->contentLayer();
    if (!layerAndroid || !layerAndroid->contentIsScrollable())
        return 0;
    *aLayer = layerAndroid;
    return layer;
}

// Scroll the RenderLayer associated with a scrollable div element.  This is
// done so that the node is visible when it is clicked.
static void scrollLayer(WebCore::RenderObject* renderer, WebCore::IntPoint* pos)
{
    LayerAndroid* aLayer;
    WebCore::RenderLayer* layer = getLayerFromRenderer(renderer, &aLayer);
    if (!layer)
        return;
    WebCore::IntRect absBounds = renderer->absoluteBoundingBoxRect();
    // Do not include the outline when moving the node's bounds.
    WebCore::IntRect layerBounds = layer->renderer()->absoluteBoundingBoxRect();

    // Move the node's bounds into the layer's coordinates.
    absBounds.move(-layerBounds.x(), -layerBounds.y());

    int diffX = layer->scrollXOffset();
    int diffY = layer->scrollYOffset();
    // Scroll the layer to the node's position.  The false parameters tell the
    // layer not to invalidate.
    layer->scrollToOffset(absBounds.x(), absBounds.y(), false, true);
    diffX = layer->scrollXOffset() - diffX;
    diffY = layer->scrollYOffset() - diffY;

    // Update the mouse position to the layer offset.
    pos->move(-diffX, -diffY);
}

// Common code for both clicking with the trackball and touchUp
bool WebViewCore::handleMouseClick(WebCore::Frame* framePtr, WebCore::Node* nodePtr)
{
    m_lastClickWasOnTextInput = false;
    bool valid = framePtr == NULL
            || CacheBuilder::validNode(m_mainFrame, framePtr, nodePtr);
    WebFrame* webFrame = WebFrame::getWebFrame(m_mainFrame);
    if (valid && nodePtr) {
    // Need to special case area tags because an image map could have an area element in the middle
    // so when attempting to get the default, the point chosen would be follow the wrong link.
        if (nodePtr->hasTagName(WebCore::HTMLNames::areaTag)) {
            webFrame->setUserInitiatedAction(true);
            nodePtr->dispatchSimulatedClick(0, true, true);
            webFrame->setUserInitiatedAction(false);
            DBG_NAV_LOG("area");
            return true;
        }

        WebCore::RenderObject* renderer = nodePtr->renderer();
        if (renderer && (renderer->isMenuList() || renderer->isListBox())) {
            WebCore::HTMLSelectElement* select = static_cast<WebCore::HTMLSelectElement*>(nodePtr);
            const WTF::Vector<WebCore::Element*>& listItems = select->listItems();
            SkTDArray<const uint16_t*> names;
            // Possible values for enabledArray.  Keep in Sync with values in
            // InvokeListBox.Container in WebView.java
            enum OptionStatus {
                OPTGROUP = -1,
                OPTION_DISABLED = 0,
                OPTION_ENABLED = 1,
            };
            SkTDArray<int> enabledArray;
            SkTDArray<int> selectedArray;
            int size = listItems.size();
            bool multiple = select->multiple();
            for (int i = 0; i < size; i++) {
                if (listItems[i]->hasTagName(WebCore::HTMLNames::optionTag)) {
                    WebCore::HTMLOptionElement* option = static_cast<WebCore::HTMLOptionElement*>(listItems[i]);
                    *names.append() = stringConverter(option->textIndentedToRespectGroupLabel());
                    *enabledArray.append() = option->disabled() ? OPTION_DISABLED : OPTION_ENABLED;
                    if (multiple && option->selected())
                        *selectedArray.append() = i;
                } else if (listItems[i]->hasTagName(WebCore::HTMLNames::optgroupTag)) {
                    WebCore::HTMLOptGroupElement* optGroup = static_cast<WebCore::HTMLOptGroupElement*>(listItems[i]);
                    *names.append() = stringConverter(optGroup->groupLabelText());
                    *enabledArray.append() = OPTGROUP;
                }
            }
            WebCoreReply* reply = new ListBoxReply(select, select->document()->frame(), this);
            // Use a pointer to HTMLSelectElement's superclass, where
            // optionToListIndex is public.
            SelectElement* selectElement = select;
            listBoxRequest(reply, names.begin(), size, enabledArray.begin(), enabledArray.count(),
                    multiple, selectedArray.begin(), multiple ? selectedArray.count() :
                    selectElement->optionToListIndex(select->selectedIndex()));
            DBG_NAV_LOG("menu list");
            return true;
        }
    }
    if (!valid || !framePtr)
        framePtr = m_mainFrame;
    if (nodePtr && valid) {
        scrollLayer(nodePtr->renderer(), &m_mousePos);
        if (nodePtr->isContentEditable() || (nodePtr->renderer()
                && (nodePtr->renderer()-> isTextArea() || nodePtr->renderer()->isTextField()))) {
            // The user clicked on a text input field.  If this causes a blur event
            // on a different text input, do not hide the keyboard in formDidBlur
            m_lastClickWasOnTextInput = true;
        }
    }
    webFrame->setUserInitiatedAction(true);
    WebCore::PlatformMouseEvent mouseDown(m_mousePos, m_mousePos, WebCore::LeftButton,
            WebCore::MouseEventPressed, 1, false, false, false, false,
            WTF::currentTime());
    // ignore the return from as it will return true if the hit point can trigger selection change
    framePtr->eventHandler()->handleMousePressEvent(mouseDown);
    WebCore::PlatformMouseEvent mouseUp(m_mousePos, m_mousePos, WebCore::LeftButton,
            WebCore::MouseEventReleased, 1, false, false, false, false,
            WTF::currentTime());
    bool handled = framePtr->eventHandler()->handleMouseReleaseEvent(mouseUp);
    webFrame->setUserInitiatedAction(false);

    m_lastClickWasOnTextInput = false;

    // If the user clicked on a textfield, make the focusController active
    // so we show the blinking cursor.
    WebCore::Node* focusNode = currentFocus();
    DBG_NAV_LOGD("m_mousePos={%d,%d} focusNode=%p handled=%s", m_mousePos.x(),
        m_mousePos.y(), focusNode, handled ? "true" : "false");
    if (focusNode) {
        WebCore::RenderObject* renderer = focusNode->renderer();
        if (renderer && (renderer->isTextField() || renderer->isTextArea())) {
            bool ime = !(static_cast<WebCore::HTMLInputElement*>(focusNode))
                    ->readOnly();
            if (ime) {
                RenderTextControl* rtc
                        = static_cast<RenderTextControl*> (renderer);
                requestKeyboardWithSelection(focusNode, rtc->selectionStart(),
                        rtc->selectionEnd());
            } else {
                requestKeyboard(false);
            }
        } else {
            // If the focusNode is contentEditable, show the keyboard and enable
            // the focus controller so the user can type.  Otherwise hide the
            // keyboard and disable the focus controller because no text input
            // is needed.
            bool keyboard = focusNode->isContentEditable();
            if (keyboard) {
                requestKeyboard(true);
            } else {
                clearTextEntry();
            }
        }
    } else {
        // There is no focusNode, so the keyboard is not needed.
        clearTextEntry();
    }
    return handled;
}

void WebViewCore::popupReply(int index)
{
    if (m_popupReply) {
        m_popupReply->replyInt(index);
        Release(m_popupReply);
        m_popupReply = 0;
    }
}

void WebViewCore::popupReply(const int* array, int count)
{
    if (m_popupReply) {
        m_popupReply->replyIntArray(array, count);
        Release(m_popupReply);
        m_popupReply = NULL;
    }
}

void WebViewCore::formDidBlur(const WebCore::Node* node)
{
    // This blur is the result of clicking on a different input.  Do not hide
    // the keyboard, since it will just be opened again.
    if (m_lastClickWasOnTextInput) return;

    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_formDidBlur, reinterpret_cast<int>(node));
    checkException(env);
}

void WebViewCore::addMessageToConsole(const WTF::String& message, unsigned int lineNumber, const WTF::String& sourceID, int msgLevel) {
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jMessageStr = env->NewString((unsigned short *)message.characters(), message.length());
    jstring jSourceIDStr = env->NewString((unsigned short *)sourceID.characters(), sourceID.length());
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_addMessageToConsole, jMessageStr, lineNumber,
            jSourceIDStr, msgLevel);
    env->DeleteLocalRef(jMessageStr);
    env->DeleteLocalRef(jSourceIDStr);
    checkException(env);
}

void WebViewCore::jsAlert(const WTF::String& url, const WTF::String& text)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jInputStr = env->NewString((unsigned short *)text.characters(), text.length());
    jstring jUrlStr = env->NewString((unsigned short *)url.characters(), url.length());
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_jsAlert, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
}

void WebViewCore::exceededDatabaseQuota(const WTF::String& url, const WTF::String& databaseIdentifier, const unsigned long long currentQuota, unsigned long long estimatedSize)
{
#if ENABLE(DATABASE)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jDatabaseIdentifierStr = env->NewString((unsigned short *)databaseIdentifier.characters(), databaseIdentifier.length());
    jstring jUrlStr = env->NewString((unsigned short *)url.characters(), url.length());
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_exceededDatabaseQuota, jUrlStr,
            jDatabaseIdentifierStr, currentQuota, estimatedSize);
    env->DeleteLocalRef(jDatabaseIdentifierStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
#endif
}

void WebViewCore::reachedMaxAppCacheSize(const unsigned long long spaceNeeded)
{
#if ENABLE(OFFLINE_WEB_APPLICATIONS)
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_reachedMaxAppCacheSize, spaceNeeded);
    checkException(env);
#endif
}

void WebViewCore::populateVisitedLinks(WebCore::PageGroup* group)
{
    m_groupForVisitedLinks = group;
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_populateVisitedLinks);
    checkException(env);
}

void WebViewCore::geolocationPermissionsShowPrompt(const WTF::String& origin)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring originString = env->NewString((unsigned short *)origin.characters(), origin.length());
    env->CallVoidMethod(m_javaGlue->object(env).get(),
                        m_javaGlue->m_geolocationPermissionsShowPrompt,
                        originString);
    env->DeleteLocalRef(originString);
    checkException(env);
}

void WebViewCore::geolocationPermissionsHidePrompt()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
                        m_javaGlue->m_geolocationPermissionsHidePrompt);
    checkException(env);
}

jobject WebViewCore::getDeviceOrientationService()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jobject object = env->CallObjectMethod(m_javaGlue->object(env).get(),
                                           m_javaGlue->m_getDeviceOrientationService);
    checkException(env);
    return object;
}

bool WebViewCore::jsConfirm(const WTF::String& url, const WTF::String& text)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jInputStr = env->NewString((unsigned short *)text.characters(), text.length());
    jstring jUrlStr = env->NewString((unsigned short *)url.characters(), url.length());
    jboolean result = env->CallBooleanMethod(m_javaGlue->object(env).get(), m_javaGlue->m_jsConfirm, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return result;
}

bool WebViewCore::jsPrompt(const WTF::String& url, const WTF::String& text, const WTF::String& defaultValue, WTF::String& result)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jInputStr = env->NewString((unsigned short *)text.characters(), text.length());
    jstring jDefaultStr = env->NewString((unsigned short *)defaultValue.characters(), defaultValue.length());
    jstring jUrlStr = env->NewString((unsigned short *)url.characters(), url.length());
    jstring returnVal = (jstring) env->CallObjectMethod(m_javaGlue->object(env).get(), m_javaGlue->m_jsPrompt, jUrlStr, jInputStr, jDefaultStr);
    // If returnVal is null, it means that the user cancelled the dialog.
    if (!returnVal)
        return false;

    result = to_string(env, returnVal);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jDefaultStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return true;
}

bool WebViewCore::jsUnload(const WTF::String& url, const WTF::String& message)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jstring jInputStr = env->NewString((unsigned short *)message.characters(), message.length());
    jstring jUrlStr = env->NewString((unsigned short *)url.characters(), url.length());
    jboolean result = env->CallBooleanMethod(m_javaGlue->object(env).get(), m_javaGlue->m_jsUnload, jUrlStr, jInputStr);
    env->DeleteLocalRef(jInputStr);
    env->DeleteLocalRef(jUrlStr);
    checkException(env);
    return result;
}

bool WebViewCore::jsInterrupt()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jboolean result = env->CallBooleanMethod(m_javaGlue->object(env).get(), m_javaGlue->m_jsInterrupt);
    checkException(env);
    return result;
}

AutoJObject
WebViewCore::getJavaObject()
{
    return getRealObject(JSC::Bindings::getJNIEnv(), m_javaGlue->m_obj);
}

jobject
WebViewCore::getWebViewJavaObject()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    return env->GetObjectField(m_javaGlue->object(env).get(), gWebViewCoreFields.m_webView);
}

void WebViewCore::updateTextSelection() {
    WebCore::Node* focusNode = currentFocus();
    if (!focusNode)
        return;
    RenderObject* renderer = focusNode->renderer();
    if (!renderer || (!renderer->isTextArea() && !renderer->isTextField()))
        return;
    RenderTextControl* rtc = static_cast<RenderTextControl*>(renderer);
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_updateTextSelection, reinterpret_cast<int>(focusNode),
            rtc->selectionStart(), rtc->selectionEnd(), m_textGeneration);
    checkException(env);
}

void WebViewCore::updateTextfield(WebCore::Node* ptr, bool changeToPassword,
        const WTF::String& text)
{
    if (m_blockTextfieldUpdates)
        return;
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    if (changeToPassword) {
        env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_updateTextfield,
                (int) ptr, true, 0, m_textGeneration);
        checkException(env);
        return;
    }
    int length = text.length();
    jstring string = env->NewString((unsigned short *) text.characters(), length);
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_updateTextfield,
            (int) ptr, false, string, m_textGeneration);
    env->DeleteLocalRef(string);
    checkException(env);
}

void WebViewCore::clearTextEntry()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
        m_javaGlue->m_clearTextEntry);
}

void WebViewCore::setBackgroundColor(SkColor c)
{
    WebCore::FrameView* view = m_mainFrame->view();
    if (!view)
        return;

    // need (int) cast to find the right constructor
    WebCore::Color bcolor((int)SkColorGetR(c), (int)SkColorGetG(c),
                          (int)SkColorGetB(c), (int)SkColorGetA(c));
    view->setBaseBackgroundColor(bcolor);

    // Background color of 0 indicates we want a transparent background
    if (c == 0)
        view->setTransparent(true);
}

jclass WebViewCore::getPluginClass(const WTF::String& libName, const char* className)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();

    jstring libString = env->NewString(libName.characters(), libName.length());
    jstring classString = env->NewStringUTF(className);
    jobject pluginClass = env->CallObjectMethod(m_javaGlue->object(env).get(),
                                           m_javaGlue->m_getPluginClass,
                                           libString, classString);
    checkException(env);

    // cleanup unneeded local JNI references
    env->DeleteLocalRef(libString);
    env->DeleteLocalRef(classString);

    if (pluginClass != NULL) {
        return static_cast<jclass>(pluginClass);
    } else {
        return NULL;
    }
}

void WebViewCore::showFullScreenPlugin(jobject childView, NPP npp)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject obj = m_javaGlue->object(env);

    env->CallVoidMethod(obj.get(),
                        m_javaGlue->m_showFullScreenPlugin, childView, (int)npp);
    checkException(env);
}

void WebViewCore::hideFullScreenPlugin()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
                        m_javaGlue->m_hideFullScreenPlugin);
    checkException(env);
}

jobject WebViewCore::addSurface(jobject view, int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    jobject result = env->CallObjectMethod(m_javaGlue->object(env).get(),
                                           m_javaGlue->m_addSurface,
                                           view, x, y, width, height);
    checkException(env);
    return result;
}

void WebViewCore::updateSurface(jobject childView, int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
                        m_javaGlue->m_updateSurface, childView,
                        x, y, width, height);
    checkException(env);
}

void WebViewCore::destroySurface(jobject childView)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_destroySurface, childView);
    checkException(env);
}

jobject WebViewCore::getContext()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    AutoJObject obj = m_javaGlue->object(env);

    jobject result = env->CallObjectMethod(obj.get(), m_javaGlue->m_getContext);
    checkException(env);
    return result;
}

bool WebViewCore::validNodeAndBounds(Frame* frame, Node* node,
    const IntRect& originalAbsoluteBounds)
{
    bool valid = CacheBuilder::validNode(m_mainFrame, frame, node);
    if (!valid)
        return false;
    RenderObject* renderer = node->renderer();
    if (!renderer)
        return false;
    IntRect absBounds = node->hasTagName(HTMLNames::areaTag)
        ? CacheBuilder::getAreaRect(static_cast<HTMLAreaElement*>(node))
        : renderer->absoluteBoundingBoxRect();
    return absBounds == originalAbsoluteBounds;
}

void WebViewCore::showRect(int left, int top, int width, int height,
        int contentWidth, int contentHeight, float xPercentInDoc,
        float xPercentInView, float yPercentInDoc, float yPercentInView)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_showRect,
            left, top, width, height, contentWidth, contentHeight,
            xPercentInDoc, xPercentInView, yPercentInDoc, yPercentInView);
    checkException(env);
}

void WebViewCore::centerFitRect(int x, int y, int width, int height)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(),
            m_javaGlue->m_centerFitRect, x, y, width, height);
    checkException(env);
}


void WebViewCore::setScrollbarModes(ScrollbarMode horizontalMode, ScrollbarMode verticalMode)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_setScrollbarModes,
            horizontalMode, verticalMode);
    checkException(env);
}

void WebViewCore::notifyWebAppCanBeInstalled()
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_setInstallableWebApp);
    checkException(env);
}

void WebViewCore::setWebTextViewAutoFillable(int queryId)
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    env->CallVoidMethod(m_javaGlue->object(env).get(), m_javaGlue->m_setWebTextViewAutoFillable, queryId);
}

bool WebViewCore::drawIsPaused() const
{
    JNIEnv* env = JSC::Bindings::getJNIEnv();
    return env->GetBooleanField(m_javaGlue->object(env).get(),
        gWebViewCoreFields.m_drawIsPaused);
}

//----------------------------------------------------------------------
// Native JNI methods
//----------------------------------------------------------------------
static jstring WebCoreStringToJString(JNIEnv *env, WTF::String string)
{
    int length = string.length();
    if (!length)
        return 0;
    jstring ret = env->NewString((jchar *)string.characters(), length);
    env->DeleteLocalRef(ret);
    return ret;
}

static void RevealSelection(JNIEnv *env, jobject obj)
{
    GET_NATIVE_VIEW(env, obj)->revealSelection();
}

static jstring RequestLabel(JNIEnv *env, jobject obj, int framePointer,
        int nodePointer)
{
    return WebCoreStringToJString(env, GET_NATIVE_VIEW(env, obj)->requestLabel(
            (WebCore::Frame*) framePointer, (WebCore::Node*) nodePointer));
}

static void ClearContent(JNIEnv *env, jobject obj)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    viewImpl->clearContent();
}

static void UpdateFrameCacheIfLoading(JNIEnv *env, jobject obj)
{
    GET_NATIVE_VIEW(env, obj)->updateFrameCacheIfLoading();
}

static void SetSize(JNIEnv *env, jobject obj, jint width, jint height,
        jint textWrapWidth, jfloat scale, jint screenWidth, jint screenHeight,
        jint anchorX, jint anchorY, jboolean ignoreHeight)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOGV("webviewcore::nativeSetSize(%u %u)\n viewImpl: %p", (unsigned)width, (unsigned)height, viewImpl);
    LOG_ASSERT(viewImpl, "viewImpl not set in nativeSetSize");
    viewImpl->setSizeScreenWidthAndScale(width, height, textWrapWidth, scale,
            screenWidth, screenHeight, anchorX, anchorY, ignoreHeight);
}

static void SetScrollOffset(JNIEnv *env, jobject obj, jint gen, jint x, jint y)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "need viewImpl");

    viewImpl->setScrollOffset(gen, x, y);
}

static void SetGlobalBounds(JNIEnv *env, jobject obj, jint x, jint y, jint h,
                            jint v)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "need viewImpl");

    viewImpl->setGlobalBounds(x, y, h, v);
}

static jboolean Key(JNIEnv *env, jobject obj, jint keyCode, jint unichar,
        jint repeatCount, jboolean isShift, jboolean isAlt, jboolean isSym,
        jboolean isDown)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    return GET_NATIVE_VIEW(env, obj)->key(PlatformKeyboardEvent(keyCode,
        unichar, repeatCount, isDown, isShift, isAlt, isSym));
}

static void Click(JNIEnv *env, jobject obj, int framePtr, int nodePtr)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in Click");

    viewImpl->click(reinterpret_cast<WebCore::Frame*>(framePtr),
        reinterpret_cast<WebCore::Node*>(nodePtr));
}

static void ContentInvalidateAll(JNIEnv *env, jobject obj)
{
    GET_NATIVE_VIEW(env, obj)->contentInvalidateAll();
}

static void DeleteSelection(JNIEnv *env, jobject obj, jint start, jint end,
        jint textGeneration)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    viewImpl->deleteSelection(start, end, textGeneration);
}

static void SetSelection(JNIEnv *env, jobject obj, jint start, jint end)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    viewImpl->setSelection(start, end);
}

static jstring ModifySelection(JNIEnv *env, jobject obj, jint direction, jint granularity)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    String selectionString = viewImpl->modifySelection(direction, granularity);
    return WebCoreStringToJString(env, selectionString);
}

static void ReplaceTextfieldText(JNIEnv *env, jobject obj,
    jint oldStart, jint oldEnd, jstring replace, jint start, jint end,
    jint textGeneration)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    WTF::String webcoreString = to_string(env, replace);
    viewImpl->replaceTextfieldText(oldStart,
            oldEnd, webcoreString, start, end, textGeneration);
}

static void PassToJs(JNIEnv *env, jobject obj,
    jint generation, jstring currentText, jint keyCode,
    jint keyValue, jboolean down, jboolean cap, jboolean fn, jboolean sym)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WTF::String current = to_string(env, currentText);
    GET_NATIVE_VIEW(env, obj)->passToJs(generation, current,
        PlatformKeyboardEvent(keyCode, keyValue, 0, down, cap, fn, sym));
}

static void ScrollFocusedTextInput(JNIEnv *env, jobject obj, jfloat xPercent,
    jint y)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    viewImpl->scrollFocusedTextInput(xPercent, y);
}

static void SetFocusControllerActive(JNIEnv *env, jobject obj, jboolean active)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    LOGV("webviewcore::nativeSetFocusControllerActive()\n");
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in nativeSetFocusControllerActive");
    viewImpl->setFocusControllerActive(active);
}

static void SaveDocumentState(JNIEnv *env, jobject obj, jint frame)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    LOGV("webviewcore::nativeSaveDocumentState()\n");
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in nativeSaveDocumentState");
    viewImpl->saveDocumentState((WebCore::Frame*) frame);
}

void WebViewCore::addVisitedLink(const UChar* string, int length)
{
    if (m_groupForVisitedLinks)
        m_groupForVisitedLinks->addVisitedLink(string, length);
}

static jint RecordContent(JNIEnv *env, jobject obj, jobject region, jobject pt)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    SkRegion* nativeRegion = GraphicsJNI::getNativeRegion(env, region);
    SkIPoint nativePt;
    BaseLayerAndroid* result = viewImpl->recordContent(nativeRegion, &nativePt);
    GraphicsJNI::ipoint_to_jpoint(nativePt, env, pt);
    return reinterpret_cast<jint>(result);
}

static void SplitContent(JNIEnv *env, jobject obj, jint content)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    viewImpl->splitContent(reinterpret_cast<PictureSet*>(content));
}

static void SendListBoxChoice(JNIEnv* env, jobject obj, jint choice)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in nativeSendListBoxChoice");
    viewImpl->popupReply(choice);
}

// Set aside a predetermined amount of space in which to place the listbox
// choices, to avoid unnecessary allocations.
// The size here is arbitrary.  We want the size to be at least as great as the
// number of items in the average multiple-select listbox.
#define PREPARED_LISTBOX_STORAGE 10

static void SendListBoxChoices(JNIEnv* env, jobject obj, jbooleanArray jArray,
        jint size)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in nativeSendListBoxChoices");
    jboolean* ptrArray = env->GetBooleanArrayElements(jArray, 0);
    SkAutoSTMalloc<PREPARED_LISTBOX_STORAGE, int> storage(size);
    int* array = storage.get();
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (ptrArray[i]) {
            array[count++] = i;
        }
    }
    env->ReleaseBooleanArrayElements(jArray, ptrArray, JNI_ABORT);
    viewImpl->popupReply(array, count);
}

static jstring FindAddress(JNIEnv *env, jobject obj, jstring addr,
    jboolean caseInsensitive)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    if (!addr)
        return 0;
    int length = env->GetStringLength(addr);
    if (!length)
        return 0;
    const jchar* addrChars = env->GetStringChars(addr, 0);
    int start, end;
    bool success = CacheBuilder::FindAddress(addrChars, length,
        &start, &end, caseInsensitive) == CacheBuilder::FOUND_COMPLETE;
    jstring ret = 0;
    if (success) {
        ret = env->NewString((jchar*) addrChars + start, end - start);
        env->DeleteLocalRef(ret);
    }
    env->ReleaseStringChars(addr, addrChars);
    return ret;
}

static jboolean HandleTouchEvent(JNIEnv *env, jobject obj, jint action,
                                 jintArray xArray, jintArray yArray, jint count, jint metaState)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    jint* ptrXArray = env->GetIntArrayElements(xArray, 0);
    jint* ptrYArray = env->GetIntArrayElements(yArray, 0);
    Vector<IntPoint> points(count);
    for (unsigned c = 0; c < count; c++) {
        points[c].setX(ptrXArray[c]);
        points[c].setY(ptrYArray[c]);
    }
    env->ReleaseIntArrayElements(xArray, ptrXArray, JNI_ABORT);
    env->ReleaseIntArrayElements(yArray, ptrYArray, JNI_ABORT);

    return viewImpl->handleTouchEvent(action, points, metaState);
}

static void TouchUp(JNIEnv *env, jobject obj, jint touchGeneration,
        jint frame, jint node, jint x, jint y)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->touchUp(touchGeneration,
        (WebCore::Frame*) frame, (WebCore::Node*) node, x, y);
}

static jstring RetrieveHref(JNIEnv *env, jobject obj, jint frame,
        jint node)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    WTF::String result = viewImpl->retrieveHref((WebCore::Frame*) frame,
            (WebCore::Node*) node);
    if (!result.isEmpty())
        return WebCoreStringToJString(env, result);
    return 0;
}

static jstring RetrieveAnchorText(JNIEnv *env, jobject obj, jint frame,
        jint node)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    WTF::String result = viewImpl->retrieveAnchorText((WebCore::Frame*) frame,
            (WebCore::Node*) node);
    if (!result.isEmpty())
        return WebCoreStringToJString(env, result);
    return 0;
}


static void MoveFocus(JNIEnv *env, jobject obj, jint framePtr, jint nodePtr)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveFocus((WebCore::Frame*) framePtr, (WebCore::Node*) nodePtr);
}

static void MoveMouse(JNIEnv *env, jobject obj, jint frame,
        jint x, jint y)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveMouse((WebCore::Frame*) frame, x, y);
}

static void MoveMouseIfLatest(JNIEnv *env, jobject obj, jint moveGeneration,
        jint frame, jint x, jint y)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->moveMouseIfLatest(moveGeneration,
        (WebCore::Frame*) frame, x, y);
}

static void UpdateFrameCache(JNIEnv *env, jobject obj)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);
    viewImpl->updateFrameCache();
}

static jint GetContentMinPrefWidth(JNIEnv *env, jobject obj)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    WebCore::Frame* frame = viewImpl->mainFrame();
    if (frame) {
        WebCore::Document* document = frame->document();
        if (document) {
            WebCore::RenderObject* renderer = document->renderer();
            if (renderer && renderer->isRenderView()) {
                return renderer->minPrefWidth();
            }
        }
    }
    return 0;
}

static void SetViewportSettingsFromNative(JNIEnv *env, jobject obj)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    WebCore::Settings* s = viewImpl->mainFrame()->page()->settings();
    if (!s)
        return;

#ifdef ANDROID_META_SUPPORT
    env->SetIntField(obj, gWebViewCoreFields.m_viewportWidth, s->viewportWidth());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportHeight, s->viewportHeight());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportInitialScale, s->viewportInitialScale());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportMinimumScale, s->viewportMinimumScale());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportMaximumScale, s->viewportMaximumScale());
    env->SetBooleanField(obj, gWebViewCoreFields.m_viewportUserScalable, s->viewportUserScalable());
    env->SetIntField(obj, gWebViewCoreFields.m_viewportDensityDpi, s->viewportTargetDensityDpi());
#endif
}

static void SetBackgroundColor(JNIEnv *env, jobject obj, jint color)
{
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->setBackgroundColor((SkColor) color);
}

static void DumpDomTree(JNIEnv *env, jobject obj, jboolean useFile)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->dumpDomTree(useFile);
}

static void DumpRenderTree(JNIEnv *env, jobject obj, jboolean useFile)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->dumpRenderTree(useFile);
}

static void DumpNavTree(JNIEnv *env, jobject obj)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    viewImpl->dumpNavTree();
}

static void DumpV8Counters(JNIEnv*, jobject)
{
#if USE(V8)
#ifdef ANDROID_INSTRUMENT
    V8Counters::dumpCounters();
#endif
#endif
}

static void SetJsFlags(JNIEnv *env, jobject obj, jstring flags)
{
#if USE(V8)
    WTF::String flagsString = to_string(env, flags);
    WTF::CString utf8String = flagsString.utf8();
    WebCore::ScriptController::setFlags(utf8String.data(), utf8String.length());
#endif
}


// Called from the Java side to set a new quota for the origin or new appcache
// max size in response to a notification that the original quota was exceeded or
// that the appcache has reached its maximum size.
static void SetNewStorageLimit(JNIEnv* env, jobject obj, jlong quota) {
#if ENABLE(DATABASE) || ENABLE(OFFLINE_WEB_APPLICATIONS)
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    Frame* frame = viewImpl->mainFrame();

    // The main thread is blocked awaiting this response, so now we can wake it
    // up.
    ChromeClientAndroid* chromeC = static_cast<ChromeClientAndroid*>(frame->page()->chrome()->client());
    chromeC->wakeUpMainThreadWithNewQuota(quota);
#endif
}

// Called from Java to provide a Geolocation permission state for the specified origin.
static void GeolocationPermissionsProvide(JNIEnv* env, jobject obj, jstring origin, jboolean allow, jboolean remember) {
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    Frame* frame = viewImpl->mainFrame();

    ChromeClientAndroid* chromeClient = static_cast<ChromeClientAndroid*>(frame->page()->chrome()->client());
    chromeClient->provideGeolocationPermissions(to_string(env, origin), allow, remember);
}

static void RegisterURLSchemeAsLocal(JNIEnv* env, jobject obj, jstring scheme) {
#ifdef ANDROID_INSTRUMENT
    TimeCounterAuto counter(TimeCounter::WebViewCoreTimeCounter);
#endif
    WebCore::SchemeRegistry::registerURLSchemeAsLocal(to_string(env, scheme));
}

static bool FocusBoundsChanged(JNIEnv* env, jobject obj)
{
    return GET_NATIVE_VIEW(env, obj)->focusBoundsChanged();
}

static void Pause(JNIEnv* env, jobject obj)
{
    // This is called for the foreground tab when the browser is put to the
    // background (and also for any tab when it is put to the background of the
    // browser). The browser can only be killed by the system when it is in the
    // background, so saving the Geolocation permission state now ensures that
    // is maintained when the browser is killed.
    ChromeClient* chromeClient = GET_NATIVE_VIEW(env, obj)->mainFrame()->page()->chrome()->client();
    ChromeClientAndroid* chromeClientAndroid = static_cast<ChromeClientAndroid*>(chromeClient);
    chromeClientAndroid->storeGeolocationPermissions();

    Frame* mainFrame = GET_NATIVE_VIEW(env, obj)->mainFrame();
    for (Frame* frame = mainFrame; frame; frame = frame->tree()->traverseNext()) {
        Geolocation* geolocation = frame->domWindow()->navigator()->optionalGeolocation();
        if (geolocation)
            geolocation->suspend();
    }

    GET_NATIVE_VIEW(env, obj)->deviceOrientationManager()->maybeSuspendClient();

    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kPause_ANPLifecycleAction;
    GET_NATIVE_VIEW(env, obj)->sendPluginEvent(event);

    GET_NATIVE_VIEW(env, obj)->setIsPaused(true);
}

static void Resume(JNIEnv* env, jobject obj)
{
    Frame* mainFrame = GET_NATIVE_VIEW(env, obj)->mainFrame();
    for (Frame* frame = mainFrame; frame; frame = frame->tree()->traverseNext()) {
        Geolocation* geolocation = frame->domWindow()->navigator()->optionalGeolocation();
        if (geolocation)
            geolocation->resume();
    }

    GET_NATIVE_VIEW(env, obj)->deviceOrientationManager()->maybeResumeClient();

    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kResume_ANPLifecycleAction;
    GET_NATIVE_VIEW(env, obj)->sendPluginEvent(event);

    GET_NATIVE_VIEW(env, obj)->setIsPaused(false);
}

static void FreeMemory(JNIEnv* env, jobject obj)
{
    ANPEvent event;
    SkANP::InitEvent(&event, kLifecycle_ANPEventType);
    event.data.lifecycle.action = kFreeMemory_ANPLifecycleAction;
    GET_NATIVE_VIEW(env, obj)->sendPluginEvent(event);
}

static void ProvideVisitedHistory(JNIEnv *env, jobject obj, jobject hist)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    LOG_ASSERT(viewImpl, "viewImpl not set in %s", __FUNCTION__);

    jobjectArray array = static_cast<jobjectArray>(hist);

    jsize len = env->GetArrayLength(array);
    for (jsize i = 0; i < len; i++) {
        jstring item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        const UChar* str = static_cast<const UChar*>(env->GetStringChars(item, NULL));
        jsize len = env->GetStringLength(item);
        viewImpl->addVisitedLink(str, len);
        env->ReleaseStringChars(item, str);
        env->DeleteLocalRef(item);
    }
}

// Notification from the UI thread that the plugin's full-screen surface has been discarded
static void FullScreenPluginHidden(JNIEnv* env, jobject obj, jint npp)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    PluginWidgetAndroid* plugin = viewImpl->getPluginWidget((NPP)npp);
    if (plugin)
        plugin->exitFullScreen(false);
}

static WebCore::IntRect jrect_to_webrect(JNIEnv* env, jobject obj)
{
    int L, T, R, B;
    GraphicsJNI::get_jrect(env, obj, &L, &T, &R, &B);
    return WebCore::IntRect(L, T, R - L, B - T);
}

static bool ValidNodeAndBounds(JNIEnv *env, jobject obj, int frame, int node,
    jobject rect)
{
    IntRect nativeRect = jrect_to_webrect(env, rect);
    return GET_NATIVE_VIEW(env, obj)->validNodeAndBounds(
            reinterpret_cast<Frame*>(frame),
            reinterpret_cast<Node*>(node), nativeRect);
}

static jobject GetTouchHighlightRects(JNIEnv* env, jobject obj, jint x, jint y, jint slop)
{
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    if (!viewImpl)
        return NULL;
    Vector<IntRect> rects = viewImpl->getTouchHighlightRects(x, y, slop);
    if (rects.isEmpty())
        return NULL;

    jclass arrayClass = env->FindClass("java/util/ArrayList");
    LOG_ASSERT(arrayClass, "Could not find java/util/ArrayList");
    jmethodID init = env->GetMethodID(arrayClass, "<init>", "(I)V");
    LOG_ASSERT(init, "Could not find constructor for ArrayList");
    jobject array = env->NewObject(arrayClass, init, rects.size());
    LOG_ASSERT(array, "Could not create a new ArrayList");
    jmethodID add = env->GetMethodID(arrayClass, "add", "(Ljava/lang/Object;)Z");
    LOG_ASSERT(add, "Could not find add method on ArrayList");
    jclass rectClass = env->FindClass("android/graphics/Rect");
    LOG_ASSERT(rectClass, "Could not find android/graphics/Rect");
    jmethodID rectinit = env->GetMethodID(rectClass, "<init>", "(IIII)V");
    LOG_ASSERT(rectinit, "Could not find init method on Rect");

    for (size_t i = 0; i < rects.size(); i++) {
        jobject rect = env->NewObject(rectClass, rectinit, rects[i].x(),
                rects[i].y(), rects[i].right(), rects[i].bottom());
        if (rect) {
            env->CallBooleanMethod(array, add, rect);
            env->DeleteLocalRef(rect);
        }
    }

    env->DeleteLocalRef(rectClass);
    env->DeleteLocalRef(arrayClass);
    return array;
}

static void AutoFillForm(JNIEnv* env, jobject obj, jint queryId)
{
#if ENABLE(WEB_AUTOFILL)
    WebViewCore* viewImpl = GET_NATIVE_VIEW(env, obj);
    if (!viewImpl)
        return;

    WebCore::Frame* frame = viewImpl->mainFrame();
    if (frame) {
        EditorClientAndroid* editorC = static_cast<EditorClientAndroid*>(frame->page()->editorClient());
        WebAutoFill* autoFill = editorC->getAutoFill();
        autoFill->fillFormFields(queryId);
    }
#endif
}

// ----------------------------------------------------------------------------

/*
 * JNI registration.
 */
static JNINativeMethod gJavaWebViewCoreMethods[] = {
    { "nativeClearContent", "()V",
            (void*) ClearContent },
    { "nativeFocusBoundsChanged", "()Z",
        (void*) FocusBoundsChanged } ,
    { "nativeKey", "(IIIZZZZ)Z",
        (void*) Key },
    { "nativeClick", "(II)V",
        (void*) Click },
    { "nativeContentInvalidateAll", "()V",
        (void*) ContentInvalidateAll },
    { "nativeSendListBoxChoices", "([ZI)V",
        (void*) SendListBoxChoices },
    { "nativeSendListBoxChoice", "(I)V",
        (void*) SendListBoxChoice },
    { "nativeSetSize", "(IIIFIIIIZ)V",
        (void*) SetSize },
    { "nativeSetScrollOffset", "(III)V",
        (void*) SetScrollOffset },
    { "nativeSetGlobalBounds", "(IIII)V",
        (void*) SetGlobalBounds },
    { "nativeSetSelection", "(II)V",
        (void*) SetSelection } ,
    { "nativeModifySelection", "(II)Ljava/lang/String;",
        (void*) ModifySelection },
    { "nativeDeleteSelection", "(III)V",
        (void*) DeleteSelection } ,
    { "nativeReplaceTextfieldText", "(IILjava/lang/String;III)V",
        (void*) ReplaceTextfieldText } ,
    { "nativeMoveFocus", "(II)V",
        (void*) MoveFocus },
    { "nativeMoveMouse", "(III)V",
        (void*) MoveMouse },
    { "nativeMoveMouseIfLatest", "(IIII)V",
        (void*) MoveMouseIfLatest },
    { "passToJs", "(ILjava/lang/String;IIZZZZ)V",
        (void*) PassToJs },
    { "nativeScrollFocusedTextInput", "(FI)V",
        (void*) ScrollFocusedTextInput },
    { "nativeSetFocusControllerActive", "(Z)V",
        (void*) SetFocusControllerActive },
    { "nativeSaveDocumentState", "(I)V",
        (void*) SaveDocumentState },
    { "nativeFindAddress", "(Ljava/lang/String;Z)Ljava/lang/String;",
        (void*) FindAddress },
    { "nativeHandleTouchEvent", "(I[I[III)Z",
            (void*) HandleTouchEvent },
    { "nativeTouchUp", "(IIIII)V",
        (void*) TouchUp },
    { "nativeRetrieveHref", "(II)Ljava/lang/String;",
        (void*) RetrieveHref },
    { "nativeRetrieveAnchorText", "(II)Ljava/lang/String;",
        (void*) RetrieveAnchorText },
    { "nativeUpdateFrameCache", "()V",
        (void*) UpdateFrameCache },
    { "nativeGetContentMinPrefWidth", "()I",
        (void*) GetContentMinPrefWidth },
    { "nativeRecordContent", "(Landroid/graphics/Region;Landroid/graphics/Point;)I",
        (void*) RecordContent },
    { "setViewportSettingsFromNative", "()V",
        (void*) SetViewportSettingsFromNative },
    { "nativeSplitContent", "(I)V",
        (void*) SplitContent },
    { "nativeSetBackgroundColor", "(I)V",
        (void*) SetBackgroundColor },
    { "nativeRegisterURLSchemeAsLocal", "(Ljava/lang/String;)V",
        (void*) RegisterURLSchemeAsLocal },
    { "nativeDumpDomTree", "(Z)V",
        (void*) DumpDomTree },
    { "nativeDumpRenderTree", "(Z)V",
        (void*) DumpRenderTree },
    { "nativeDumpNavTree", "()V",
        (void*) DumpNavTree },
    { "nativeDumpV8Counters", "()V",
        (void*) DumpV8Counters },
    { "nativeSetNewStorageLimit", "(J)V",
        (void*) SetNewStorageLimit },
    { "nativeGeolocationPermissionsProvide", "(Ljava/lang/String;ZZ)V",
        (void*) GeolocationPermissionsProvide },
    { "nativePause", "()V", (void*) Pause },
    { "nativeResume", "()V", (void*) Resume },
    { "nativeFreeMemory", "()V", (void*) FreeMemory },
    { "nativeSetJsFlags", "(Ljava/lang/String;)V", (void*) SetJsFlags },
    { "nativeRequestLabel", "(II)Ljava/lang/String;",
        (void*) RequestLabel },
    { "nativeRevealSelection", "()V", (void*) RevealSelection },
    { "nativeUpdateFrameCacheIfLoading", "()V",
        (void*) UpdateFrameCacheIfLoading },
    { "nativeProvideVisitedHistory", "([Ljava/lang/String;)V",
        (void*) ProvideVisitedHistory },
    { "nativeFullScreenPluginHidden", "(I)V",
        (void*) FullScreenPluginHidden },
    { "nativeValidNodeAndBounds", "(IILandroid/graphics/Rect;)Z",
        (void*) ValidNodeAndBounds },
    { "nativeGetTouchHighlightRects", "(III)Ljava/util/ArrayList;",
        (void*) GetTouchHighlightRects },
    { "nativeAutoFillForm", "(I)V",
        (void*) AutoFillForm },
};

int register_webviewcore(JNIEnv* env)
{
    jclass widget = env->FindClass("android/webkit/WebViewCore");
    LOG_ASSERT(widget,
            "Unable to find class android/webkit/WebViewCore");
    gWebViewCoreFields.m_nativeClass = env->GetFieldID(widget, "mNativeClass",
            "I");
    LOG_ASSERT(gWebViewCoreFields.m_nativeClass,
            "Unable to find android/webkit/WebViewCore.mNativeClass");
    gWebViewCoreFields.m_viewportWidth = env->GetFieldID(widget,
            "mViewportWidth", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportWidth,
            "Unable to find android/webkit/WebViewCore.mViewportWidth");
    gWebViewCoreFields.m_viewportHeight = env->GetFieldID(widget,
            "mViewportHeight", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportHeight,
            "Unable to find android/webkit/WebViewCore.mViewportHeight");
    gWebViewCoreFields.m_viewportInitialScale = env->GetFieldID(widget,
            "mViewportInitialScale", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportInitialScale,
            "Unable to find android/webkit/WebViewCore.mViewportInitialScale");
    gWebViewCoreFields.m_viewportMinimumScale = env->GetFieldID(widget,
            "mViewportMinimumScale", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportMinimumScale,
            "Unable to find android/webkit/WebViewCore.mViewportMinimumScale");
    gWebViewCoreFields.m_viewportMaximumScale = env->GetFieldID(widget,
            "mViewportMaximumScale", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportMaximumScale,
            "Unable to find android/webkit/WebViewCore.mViewportMaximumScale");
    gWebViewCoreFields.m_viewportUserScalable = env->GetFieldID(widget,
            "mViewportUserScalable", "Z");
    LOG_ASSERT(gWebViewCoreFields.m_viewportUserScalable,
            "Unable to find android/webkit/WebViewCore.mViewportUserScalable");
    gWebViewCoreFields.m_viewportDensityDpi = env->GetFieldID(widget,
            "mViewportDensityDpi", "I");
    LOG_ASSERT(gWebViewCoreFields.m_viewportDensityDpi,
            "Unable to find android/webkit/WebViewCore.mViewportDensityDpi");
    gWebViewCoreFields.m_webView = env->GetFieldID(widget,
            "mWebView", "Landroid/webkit/WebView;");
    LOG_ASSERT(gWebViewCoreFields.m_webView,
            "Unable to find android/webkit/WebViewCore.mWebView");
    gWebViewCoreFields.m_drawIsPaused = env->GetFieldID(widget,
            "mDrawIsPaused", "Z");
    LOG_ASSERT(gWebViewCoreFields.m_drawIsPaused,
            "Unable to find android/webkit/WebViewCore.mDrawIsPaused");

    return jniRegisterNativeMethods(env, "android/webkit/WebViewCore",
            gJavaWebViewCoreMethods, NELEM(gJavaWebViewCoreMethods));
}

} /* namespace android */
