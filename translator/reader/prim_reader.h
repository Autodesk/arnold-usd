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

#include <ai_msg.h>
#include <ai_nodes.h>
#include <pxr/usd/usd/prim.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "reader.h"
#include "utils.h"

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *   Base Class for a UsdPrim read. This class is in charge of converting a USD
 *primitive to Arnold
 *
 **/
class UsdArnoldPrimReader {
public:
    UsdArnoldPrimReader() {}
    virtual ~UsdArnoldPrimReader() {}

    virtual void read(const UsdPrim &prim, UsdArnoldReaderContext &context) = 0;

    static inline bool vtValueGetBool(const VtValue& value)
    {
        if (value.IsHolding<bool>())
            return value.UncheckedGet<bool>();
        if (value.IsHolding<int>())
            return value.UncheckedGet<int>() != 0;
        if (value.IsHolding<long>())
            return value.UncheckedGet<long>() != 0;

        return value.Get<bool>();
    }
    static inline float vtValueGetFloat(const VtValue& value)
    {
        if (value.IsHolding<float>())
            return value.UncheckedGet<float>();
        if (value.IsHolding<double>())
            return static_cast<float>(value.UncheckedGet<double>());
        
        return value.Get<float>();
    }
    static inline unsigned char vtValueGetByte(const VtValue& value)
    {
        if (value.IsHolding<int>())
            return static_cast<unsigned char>(value.UncheckedGet<int>());
        if (value.IsHolding<long>())
            return static_cast<unsigned char>(value.UncheckedGet<long>());
        if (value.IsHolding<unsigned char>())
            return value.UncheckedGet<unsigned char>();
            
        return value.Get<unsigned char>();
    }
    static inline int vtValueGetInt(const VtValue& value)
    {
        if (value.IsHolding<int>())
            return value.UncheckedGet<int>();
        if (value.IsHolding<long>())
            return static_cast<int>(value.UncheckedGet<long>());

        return value.Get<int>();
    }
protected:
    void readArnoldParameters(
        const UsdPrim &prim, UsdArnoldReaderContext &context, AtNode *node, const TimeSettings &time, const std::string &scope = "arnold");

};

class UsdArnoldReadUnsupported : public UsdArnoldPrimReader {
public:
    UsdArnoldReadUnsupported(const std::string &type) : UsdArnoldPrimReader(), _type(type) {}
    void read(const UsdPrim &prim, UsdArnoldReaderContext &context) override;

private:
    std::string _type;
};

#define REGISTER_PRIM_READER(name)                                                                   \
    class name : public UsdArnoldPrimReader {                                                        \
    public:                                                                                          \
        void read(const UsdPrim &prim, UsdArnoldReaderContext &context) override; \
    };
