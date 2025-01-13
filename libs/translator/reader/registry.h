//
// SPDX-License-Identifier: Apache-2.0
//

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
#include <ai.h>

#include <pxr/usd/usd/prim.h>

#include <string>
#include <unordered_map>
#include <vector>

class UsdArnoldPrimReader;

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *  This Registry stores which UsdArnoldPrimReader must be used to read a
 *UsdPrim of a given type. In its constructor, it will iterate over all known
 *arnold node types and register the corresponding UsdArnoldPrimReaders. This
 *class can be derived if we need to customize the list of prim readers to be
 *used
 **/

class UsdArnoldReaderRegistry {
public:
    UsdArnoldReaderRegistry() {}
    virtual ~UsdArnoldReaderRegistry();

    virtual void RegisterPrimitiveReaders();
    // Register a new prim reader to this type of usd primitive.
    // If an existing one was previously registed for this same type, it will be
    // deleted and overridden
    void RegisterReader(const std::string &primName, UsdArnoldPrimReader *primReader);

    // Clear all the registered prim readers
    void Clear();

    UsdArnoldPrimReader *GetPrimReader(const std::string &primName)
    {
        std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator it = _readersMap.find(primName);
        if (it != _readersMap.end())
            return it->second;

        size_t pos = primName.find_last_of('_');
        if (pos != std::string::npos && pos < primName.length() - 1) {
            // To support versioning, in a first step we just remove the versioning suffix. 
            // In the long term we'll need to use the exact schema version in the translators.
            bool hasVersion = true;
            std::locale loc;
            for (size_t i = pos + 1; i < primName.length(); ++i) {
                if (!std::isdigit(primName[i], loc)) {
                    hasVersion = false;
                    break;
                }
            }
            if (hasVersion) {
                std::string unversionedPrimName = primName.substr(0, pos);
                it = _readersMap.find(unversionedPrimName);
                if (it != _readersMap.end())
                    return it->second;
            }
        }
        return nullptr; // return NULL if no reader was registered for this
                            // node type, it will be skipped
    }

private:
    std::unordered_map<std::string, UsdArnoldPrimReader *> _readersMap;
};

// The viewport API is introduced in Arnold 6.0.0. I
// It defines AtProcViewportMode and AtParamValueMap, which are needed by this class
#if AI_VERSION_ARCH_NUM >= 6

/**
 *  This registry is used for viewport display of the USD procedural.
 *  It can read the "Boundable" geometries as boxes, PointBased geometries as points,
 *  or Mesh geometries as polymeshes, depending on the viewport settings.
 **/
class UsdArnoldViewportReaderRegistry : public UsdArnoldReaderRegistry {
public:
    UsdArnoldViewportReaderRegistry(AtProcViewportMode mode, const AtParamValueMap *params)
        : UsdArnoldReaderRegistry(),
        _mode(mode), _params(params)
    {
    }
    virtual ~UsdArnoldViewportReaderRegistry() {}

    void RegisterPrimitiveReaders() override;

private:
    AtProcViewportMode _mode;
    const AtParamValueMap *_params;
};

#endif
