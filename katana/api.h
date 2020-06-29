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
/// @file katana/api.h
///
/// API definitions for exports and imports.
#pragma once

#include <pxr/base/arch/export.h>
#include <pxr/pxr.h>

#if defined(PXR_STATIC)
#define KATANAARNOLD_API
#define KATANAARNOLD_API_TEMPLATE_CLASS(...)
#define KATANAARNOLD_API_TEMPLATE_STRUCT(...)
#define KATANAARNOLD_LOCAL
#else
#if defined(KATANAARNOLD_EXPORTS)
#define KATANAARNOLD_API ARCH_EXPORT
#define KATANAARNOLD_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#define KATANAARNOLD_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#else
#define KATANAARNOLD_API ARCH_IMPORT
#define KATANAARNOLD_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#define KATANAARNOLD_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#endif
#define KATANAARNOLD_LOCAL ARCH_HIDDEN
#endif
