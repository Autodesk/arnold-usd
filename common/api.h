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
/// @file common/api.h
///
/// API definitions for exports and imports.
#pragma once

#include <pxr/base/arch/export.h>
#include <pxr/pxr.h>

#if defined(PXR_STATIC)
#define ARNOLDUSD_API
#define ARNOLDUSD_API_TEMPLATE_CLASS(...)
#define ARNOLDUSD_API_TEMPLATE_STRUCT(...)
#define ARNOLDUSD_LOCAL
#else
#if defined(ARNOLDUSD_EXPORTS)
#define ARNOLDUSD_API ARCH_EXPORT
#define ARNOLDUSD_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#define ARNOLDUSD_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#else
#define ARNOLDUSD_API ARCH_IMPORT
#define ARNOLDUSD_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#define ARNOLDUSD_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#endif
#define ARNOLDUSD_LOCAL ARCH_HIDDEN
#endif
