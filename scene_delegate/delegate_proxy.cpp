// Copyright 2021 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "delegate_proxy.h"

#include "delegate.h"

PXR_NAMESPACE_OPEN_SCOPE

ImagingArnoldDelegateProxy::ImagingArnoldDelegateProxy(ImagingArnoldDelegate* delegate) : _delegate(delegate) {}

bool ImagingArnoldDelegateProxy::IsRprimSupported(const TfToken& typeId) const
{
    return _delegate->GetRenderIndex().IsRprimTypeSupported(typeId);
}

bool ImagingArnoldDelegateProxy::IsBprimSupported(const TfToken& typeId) const
{
    return _delegate->GetRenderIndex().IsBprimTypeSupported(typeId);
}

bool ImagingArnoldDelegateProxy::IsSprimSupported(const TfToken& typeId) const
{
    return _delegate->GetRenderIndex().IsSprimTypeSupported(typeId);
}

void ImagingArnoldDelegateProxy::InsertRprim(const TfToken& typeId, const SdfPath& id)
{
    _delegate->GetRenderIndex().InsertRprim(typeId, _delegate, id);
}

void ImagingArnoldDelegateProxy::InsertBprim(const TfToken& typeId, const SdfPath& id)
{
    _delegate->GetRenderIndex().InsertBprim(typeId, _delegate, id);
}

void ImagingArnoldDelegateProxy::InsertSprim(const TfToken& typeId, const SdfPath& id)
{
    _delegate->GetRenderIndex().InsertSprim(typeId, _delegate, id);
}

PXR_NAMESPACE_CLOSE_SCOPE
