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
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldWriterRegistry;

/**
 *  Class handling the translation of Arnold data to USD.
 *  A registry provides the desired primWriter for a given Arnold node entry
 *name.
 **/

class UsdArnoldWriter {
public:
    UsdArnoldWriter()
        : _universe(nullptr),
          _registry(nullptr),
          _writeBuiltin(true),
          _mask(AI_NODE_ALL),
          _shutterStart(0.f),
          _shutterEnd(0.f)
    {
    }
    ~UsdArnoldWriter() {}

    void Write(const AtUniverse *universe);  // convert a given arnold universe
    void WritePrimitive(const AtNode *node); // write a primitive (node)

    void SetRegistry(UsdArnoldWriterRegistry *registry);

    void SetUsdStage(UsdStageRefPtr stage) { _stage = stage; }
    const UsdStageRefPtr &GetUsdStage() { return _stage; }

    void SetUniverse(const AtUniverse *universe) { _universe = universe; }
    const AtUniverse *GetUniverse() const { return _universe; }

    void SetWriteBuiltin(bool b) { _writeBuiltin = b; }
    bool GetWriteBuiltin() const { return _writeBuiltin; }

    void SetMask(int m) { _mask = m; }
    int GetMask() const { return _mask; }

    float GetShutterStart() const { return _shutterStart; }
    float GetShutterEnd() const { return _shutterEnd; }

    bool IsNodeExported(const AtString &name) { return _exportedNodes.count(name) == 1; }

private:
    const AtUniverse *_universe;        // Arnold universe to be converted
    UsdArnoldWriterRegistry *_registry; // custom registry used for this writer. If null, a global
                                        // registry will be used.
    UsdStageRefPtr _stage;              // USD stage where the primitives are added
    bool _writeBuiltin;                 // do we want to create usd-builtin primitives, or arnold schemas
    int _mask;                          // Mask based on arnold flags (AI_NODE_SHADER, etc...),
                                        // determining what arnold nodes must be saved out
    float _shutterStart;
    float _shutterEnd;
    std::unordered_set<AtString, AtStringHash> _exportedNodes; // list of arnold attributes that were exported
};
