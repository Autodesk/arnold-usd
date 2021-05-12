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
/// @file scene_delegate/delegate_proxy.h
///
/// Class and utilities for interacting with the Scene Delegate while hiding most of the functionality.
#pragma once
#include "api.h"

#include <pxr/base/tf/token.h>

#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class ImagingArnoldDelegate;

/// @class ImagingArnoldDelegateProxy
///
/// Utility class to interact with the Hydra render index, without exposing the whole scene delegate.
class ImagingArnoldDelegateProxy {
public:
    /// Constructor for ImagingArnoldDelegateProxy
    ///
    /// @param delegate Pointer to ImagingArnoldDelegate.
    IMAGINGARNOLD_API
    ImagingArnoldDelegateProxy(ImagingArnoldDelegate* delegate);

    /// Tells if a given rprim type is supported.
    ///
    /// @param typeId Type of the rprim.
    /// @return True if the rprim is supported, false otherwise.
    IMAGINGARNOLD_API
    bool IsRprimSupported(const TfToken& typeId) const;
    /// Tells if a given bprim type is supported.
    ///
    /// @param typeId Type of the bprim.
    /// @return True if the bprim is supported, false otherwise.
    IMAGINGARNOLD_API
    bool IsBprimSupported(const TfToken& typeId) const;
    /// Tells if a given sprim type is supported.
    ///
    /// @param typeId Type of the sprim.
    /// @return True if the sprim is supported, false otherwise.
    IMAGINGARNOLD_API
    bool IsSprimSupported(const TfToken& typeId) const;
    /// Inserts a new rprim in the render index.
    ///
    /// @param typeId Type of the rprim.
    /// @param id Path of the rprim.
    IMAGINGARNOLD_API
    void InsertRprim(const TfToken& typeId, const SdfPath& id);
    /// Inserts a new bprim in the render index.
    ///
    /// @param typeId Type of the bprim.
    /// @param id Path of the bprim.
    IMAGINGARNOLD_API
    void InsertBprim(const TfToken& typeId, const SdfPath& id);
    /// Inserts a new sprim in the render index.
    ///
    /// @param typeId Type of the sprim.
    /// @param id Path of the sprim.
    IMAGINGARNOLD_API
    void InsertSprim(const TfToken& typeId, const SdfPath& id);

private:
    /// Non-owning pointer to the ImagingArnoldDelegate.
    ImagingArnoldDelegate* _delegate = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
