// Copyright 2019 Autodesk, Inc.
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

#include <ai_nodes.h>

#include <pxr/usd/usd/prim.h>

#include <string>
#include <unordered_map>
#include <vector>

class UsdArnoldPrimWriter;

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *   The Registry stores a map returning which prim writer should be used for a
 *given Arnold node entry. It is similar to the "reader" registry. Node types
 *that were not registered will be skipped from the export
 **/

class UsdArnoldWriterRegistry {
public:
    UsdArnoldWriterRegistry(bool writeBuiltin = true);
    virtual ~UsdArnoldWriterRegistry();

    // Register a new prim writer to this type of usd primitive.
    // If an existing one was previously registed for this same type, it will be
    // deleted and overridden
    void RegisterWriter(const std::string &primName, UsdArnoldPrimWriter *primWriter);

    UsdArnoldPrimWriter *GetPrimWriter(const std::string &primName)
    {
        std::unordered_map<std::string, UsdArnoldPrimWriter *>::iterator it = _writersMap.find(primName);
        if (it == _writersMap.end())
            return nullptr; // return NULL if no writer was registered for this
                            // node type, it will be skipped

        return it->second;
    }

private:
    std::unordered_map<std::string, UsdArnoldPrimWriter *> _writersMap;
};
