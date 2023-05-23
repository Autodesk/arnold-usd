// Copyright 2022 Autodesk, Inc.
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
#pragma once

#include <pxr/base/arch/export.h>
#include <pxr/pxr.h>

#if defined(PXR_STATIC)
#define USDIMAGINGARNOLD_API
#define USDIMAGINGARNOLD_API_TEMPLATE_CLASS(...)
#define USDIMAGINGARNOLD_API_TEMPLATE_STRUCT(...)
#define USDIMAGINGARNOLD_LOCAL
#else
#if defined(USDIMAGINGARNOLD_EXPORTS)
#define USDIMAGINGARNOLD_API ARCH_EXPORT
#define USDIMAGINGARNOLD_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#define USDIMAGINGARNOLD_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#else
#define USDIMAGINGARNOLD_API ARCH_IMPORT
#define USDIMAGINGARNOLD_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#define USDIMAGINGARNOLD_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#endif
#define USDIMAGINGARNOLD_LOCAL ARCH_HIDDEN
#endif
