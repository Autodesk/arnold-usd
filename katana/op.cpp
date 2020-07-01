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

#include <FnGeolib/op/FnGeolibOp.h>

#include <usdKatana/usdInPluginRegistry.h>

#include "material.h"

PXR_NAMESPACE_USING_DIRECTIVE

PXRUSDKATANA_USDIN_PLUGIN_DECLARE(KatanaArnold_MaterialDecorator)
DEFINE_GEOLIBOP_PLUGIN(KatanaArnold_MaterialDecorator)
PXRUSDKATANA_USDIN_PLUGIN_DEFINE(KatanaArnold_MaterialDecorator, privateData, opArgs, interface)
{
    modifyMaterial(privateData, opArgs, interface);
}

void registerPlugins()
{
    USD_OP_REGISTER_PLUGIN(KatanaArnold_MaterialDecorator, "KatanaArnold_MaterialDecorator", 0, 1);
    PxrUsdKatanaUsdInPluginRegistry::RegisterLocationDecoratorOp("KatanaArnold_MaterialDecorator");
}
