/*
 * Copyright (c) 2012 Motorola Mobility, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY MOTOROLA MOBILITY, INC. AND ITS CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MOTOROLA MOBILITY, INC. OR ITS
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RadioNodeList.h"

#include "Element.h"
#include "HTMLFormElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"

namespace WebCore {

using namespace HTMLNames;

RadioNodeList::RadioNodeList(const AtomicString& name, Element* formElement)
    : DynamicSubtreeNodeList(formElement->document())
    , m_name(name)
    , m_formElement(formElement)
{
    m_formElement->document()->registerDynamicSubtreeNodeList(this);
}

RadioNodeList::~RadioNodeList()
{
    m_formElement->removeCachedRadioNodeList(this, m_name);
    m_formElement->document()->unregisterDynamicSubtreeNodeList(this);
}

static inline HTMLInputElement* toRadioButtonInputElement(Node* node)
{
    ASSERT(node->isElementNode());
    HTMLInputElement* inputElement = node->toInputElement();
    if (!inputElement || !inputElement->isRadioButton() || inputElement->value().isEmpty())
        return 0;
    return inputElement;
}

String RadioNodeList::value() const
{
    for (unsigned i = 0; i < length(); ++i) {
        Node* node = item(i);
        const HTMLInputElement* inputElement = toRadioButtonInputElement(node);
        if (!inputElement || !inputElement->checked())
            continue;
        return inputElement->value();
    }
    return String();
}

void RadioNodeList::setValue(const String& value)
{
    for (unsigned i = 0; i < length(); ++i) {
        Node* node = item(i);
        HTMLInputElement* inputElement = toRadioButtonInputElement(node);
        if (!inputElement || inputElement->value() != value)
            continue;
        inputElement->setChecked(true);
        return;
    }
}

bool RadioNodeList::nodeMatches(Element* testElement) const
{
    if (!testElement->isFormControlElement())
        return false;

    HTMLFormElement* formElement = static_cast<HTMLFormControlElement*>(testElement)->form();
    if (!formElement || formElement != m_formElement)
        return false;

    if (HTMLInputElement* inputElement = testElement->toInputElement()) {
        if (inputElement->isImageButton())
            return false;
    }

    return equalIgnoringCase(testElement->getIdAttribute(), m_name) || equalIgnoringCase(testElement->getNameAttribute(), m_name);
}

} // namspace

