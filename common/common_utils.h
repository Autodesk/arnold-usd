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
/// @file common_utils.h
///
/// Common utils.
#include <string>

#include <pxr/pxr.h>

#include <pxr/base/arch/export.h>

PXR_NAMESPACE_OPEN_SCOPE

// convert from "snake_case" to "camelCase"
// ignores the capitalization of input strings: letters are only capitalized
// if they follow an underscore
//
ARCH_HIDDEN
std::string MakeCamelCase(const std::string &in);

PXR_NAMESPACE_CLOSE_SCOPE
