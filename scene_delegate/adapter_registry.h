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
/// @file scene_delegate/adapter_registry.h
///
/// Registry for scene delegate adapters.
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/tf/singleton.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

class ImagingArnoldPrimAdapter;

class ImagingArnoldAdapterRegistry : public TfSingleton<ImagingArnoldAdapterRegistry> {
private:
    friend class TfSingleton<ImagingArnoldAdapterRegistry>;
    ImagingArnoldAdapterRegistry();
    ~ImagingArnoldAdapterRegistry();

public:
    static ImagingArnoldAdapterRegistry& GetInstance()
    {
        return TfSingleton<ImagingArnoldAdapterRegistry>::GetInstance();
    }

    IMAGINGARNOLD_API
    ImagingArnoldPrimAdapter* FindAdapter(const AtString& arnoldType) const;

    IMAGINGARNOLD_API
    void RegisterAdapter(const AtString& arnoldType, ImagingArnoldPrimAdapter* adapter);
};

IMAGINGARNOLD_API_TEMPLATE_CLASS(TfSingleton<ImagingArnoldAdapterRegistry>);

PXR_NAMESPACE_CLOSE_SCOPE
