// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class NdrAiParserPlugin : public NdrParserPlugin {
public:
    NDRAI_API
    NdrAiParserPlugin();

    NDRAI_API
    ~NdrAiParserPlugin() override;

    NDRAI_API
    NdrNodeUniquePtr Parse(
        const NdrNodeDiscoveryResult& discoveryResult) override;

    NDRAI_API
    const NdrTokenVec& GetDiscoveryTypes() const override;

    NDRAI_API
    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
