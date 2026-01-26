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
#include "common_utils.h"
#include "constant_strings.h"

PXR_NAMESPACE_OPEN_SCOPE

std::string ArnoldUsdMakeCamelCase(const std::string &in)
{
    std::string out;
    out.reserve(in.length());
    bool capitalize = false;
    unsigned char c;
    for (size_t i = 0; i < in.length(); ++i) {
        c = in[i];
        if (c == '_') {
            capitalize = true;
        } else {
            if (capitalize) {
                c = toupper(c);
                capitalize = false;
            }
            out += c;
        }
    }
    return out;
}


int ArnoldUsdGetLogVerbosityFromFlags(int flags)
{
    // This isn't an exact mapping, as verbosity can't emcompass all possible
    // flag combinations... so we just check for certain flags, and assume
    if (flags == 0) {
        return 0;
    };
    if (flags & AI_LOG_DEBUG) {
        return 5;
    }
    if (flags & (AI_LOG_STATS | AI_LOG_PLUGINS)) {
        return 4;
    }
    if (flags & (AI_LOG_INFO | AI_LOG_PROGRESS)) {
        return 3;
    }
    if (flags & (AI_LOG_WARNINGS)) {
        return 2;
    }
    return 1;
}

void ArnoldUsdApplyParentMatrix(AtNode *node, const AtNode *parent)
{
    if (parent == nullptr || node == nullptr)
        return;
    AtArray *matrices = AiNodeGetArray(node, str::matrix);
    unsigned int matrixNumKeys = matrices  ? AiArrayGetNumKeys(matrices) : 0;
    if (matrixNumKeys == 0)
        return;

    while (parent) {
        const AtArray *parentMatrices = AiNodeGetArray(parent, str::matrix);
        unsigned int parentMatrixNumKeys = parentMatrices && AiArrayGetNumElements(parentMatrices) > 0 ?
            AiArrayGetNumKeys(parentMatrices) : 0;
        if (parentMatrixNumKeys > 0) {
            
            for (int i = 0; i < matrixNumKeys; ++i) {
                AtMatrix m = AiArrayGetMtx(matrices, i);
                float t = (float)i / AiMax(float(matrixNumKeys - 1), 1.f);
                m = AiM4Mult(m, AiArrayInterpolateMtx(parentMatrices, t, 0));
                AiArraySetMtx(matrices, i, m);
            }
        }
        parent = AiNodeGetParent(parent);
    }
    AiNodeSetArray(node, str::matrix, matrices);
}

PXR_NAMESPACE_CLOSE_SCOPE
