// Copyright 2020 Autodesk, Inc.
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
/// @file katana/material.h
///
/// Tools for editing material locations during import.
#pragma once

#include "api.h"

#include <pxr/pxr.h>

#include <usdKatana/usdInPluginRegistry.h>

PXR_NAMESPACE_OPEN_SCOPE

KATANAARNOLD_API
void modifyMaterial(
    const PxrUsdKatanaUsdInPrivateData& privateData, FnKat::GroupAttribute opArgs,
    FnKat::GeolibCookInterface& interface);

PXR_NAMESPACE_CLOSE_SCOPE
