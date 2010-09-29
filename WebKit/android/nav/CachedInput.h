/*
 * Copyright 2009, The Android Open Source Project
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

#ifndef CachedInput_H
#define CachedInput_H

#include "CachedDebug.h"
#include "HTMLInputElement.h"
#include "PlatformString.h"

namespace android {

class CachedInput {
public:
    CachedInput() {
        // Initiaized to 0 in its array, so nothing to do in the
        // constructor
    }
    void* formPointer() const { return mForm; }
    void init();
    WebCore::HTMLInputElement::DeprecatedInputType inputType() const { return mInputType; }
    bool isRtlText() const { return mIsRtlText; }
    bool isTextField() const { return mIsTextField; }
    int maxLength() const { return mMaxLength; };
    const WTF::String& name() const { return mName; }
    void setFormPointer(void* form) { mForm = form; }
    void setInputType(WebCore::HTMLInputElement::DeprecatedInputType type) { mInputType = type; }
    void setIsRtlText(bool isRtlText) { mIsRtlText = isRtlText; }
    void setIsTextField(bool isTextField) { mIsTextField = isTextField; }
    void setMaxLength(int maxLength) { mMaxLength = maxLength; }
    void setName(const WTF::String& name) { mName = name; }
    void setTextSize(int textSize) { mTextSize = textSize; }
    int textSize() const { return mTextSize; }
private:
    void* mForm;
    WTF::String mName;
    int mMaxLength;
    int mTextSize;
    WebCore::HTMLInputElement::DeprecatedInputType mInputType;
    bool mIsRtlText : 1;
    bool mIsTextField : 1;
#if DUMP_NAV_CACHE
public:
    class Debug {
public:
        CachedInput* base() const;
        void print() const;
    } mDebug;
#endif
};

}

#endif
