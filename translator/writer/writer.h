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
    UsdArnoldWriter() : _universe(NULL), _registry(NULL), _writeBuiltin(true) {}
    ~UsdArnoldWriter() {}

    void write(const AtUniverse *universe);  // convert a given arnold universe
    void writePrimitive(const AtNode *node); // write a primitive (node)

    void setRegistry(UsdArnoldWriterRegistry *registry);

    void setUsdStage(UsdStageRefPtr stage) { _stage = stage; }
    const UsdStageRefPtr &getUsdStage() { return _stage; }

    void setUniverse(const AtUniverse *universe) { _universe = universe; }
    const AtUniverse *getUniverse() const { return _universe; }

    void setWriteBuiltin(bool b) { _writeBuiltin = b;}
    bool getWriteBuiltin() const { return _writeBuiltin;}

    bool isNodeExported(const AtString &name) { return _exportedNodes.count(name) == 1;}


private:
    const AtUniverse *_universe;        // Arnold universe to be converted
    UsdArnoldWriterRegistry *_registry; // custom registry used for this writer. If null, a global
                                        // registry will be used.
    UsdStageRefPtr _stage;              // USD stage where the primitives are added
    bool _writeBuiltin;                 // do we want to create usd-builtin primitives, or arnold schemas
    std::unordered_set<AtString, AtStringHash> _exportedNodes; // list of arnold attributes that were exported
};
