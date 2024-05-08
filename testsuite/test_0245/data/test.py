import os
import sys

arnold_plugin_paths = os.environ['ARNOLD_PLUGIN_PATH']

def find_in_file(expectedTypes, filename):
    with open(filename, 'r') as f:
        lines = f.readlines()
        for expectedType in expectedTypes:
            found = False
            for line in lines:
                if '"{}"'.format(expectedType) in line:
                    found = True
                    break
            if not found:
                print('Type {} not found in {}'.format(expectedType, filename))
                return False
        return True
    return False
    
def test_schemas(arnold_plugin):
    expectedSchemas = ['ArnoldAlembic', 
                    'ArnoldCameraAPI', 
                    'ArnoldCamera', 
                    'ArnoldCone', 
                    'ArnoldCurves', 
                    'ArnoldCurvesAPI',
                    'ArnoldDriverExr',
                    'ArnoldGinstance',
                    'ArnoldImagerColorCorrect',
                    'ArnoldImagerDenoiserOidn',
                    'ArnoldInstancer',
                    'ArnoldLightAPI',
                    'ArnoldMaterialx',
                    'ArnoldNodeGraph',
                    'ArnoldOrthoCamera',
                    'ArnoldPerspCameraAPI',
                    'ArnoldPointLightAPI',
                    'ArnoldPoints',
                    'ArnoldPointsAPI',
                    'ArnoldPolymesh',
                    'ArnoldPolymeshAPI',
                    'ArnoldProcedural',
                    'ArnoldSkydomeLightAPI',
                    'ArnoldVrCamera']

    filename = os.path.join(arnold_plugin, 'usd', 'usdArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        return False
    if not find_in_file(expectedSchemas, filename):
        return False

    filename = os.path.join(arnold_plugin, 'usd', 'usdArnold', 'resources', 'generatedSchema.usda')
    if not os.path.exists(filename):
        return False
    if not find_in_file(expectedSchemas, filename):
        return False
    return True

def test_ndr(arnold_plugin):
    expectedNdr = ["NdrArnoldParserPlugin", "NdrArnoldDiscoveryPlugin"]
    filename = os.path.join(arnold_plugin, 'usd', 'ndrArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        return False
    if not find_in_file(expectedNdr, filename):
        return False

    expectedImaging = ["UsdImagingArnoldAlembicAdapter",
                        "ArnoldAlembic",
                        "UsdImagingArnoldPolymeshAdapter",
                        "ArnoldPolymesh",
                        "UsdImagingArnoldProceduralAdapter",
                        "ArnoldProcedural",
                        "UsdImagingArnoldUsdAdapter",
                        "ArnoldUsd",
                        "UsdImagingArnoldUsdLuxLightFilterAdapter",
                        "ArnoldNodeGraphAdapter",
                        "ArnoldProceduralCustomAdapter",
                        "ArnoldProceduralCustom"]
                        
    filename = os.path.join(arnold_plugin, 'usd', 'usdImagingArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        return False
    if not find_in_file(expectedImaging, filename):
        return False

    return True

arnold_plugin_paths = arnold_plugin_paths.split(os.pathsep)

found_schemas = False
found_ndr = False
for arnold_plugin in arnold_plugin_paths:
    found_schemas |= test_schemas(arnold_plugin)
    found_ndr |= test_ndr(arnold_plugin)

if found_schemas and found_ndr:
    print('SUCCESS')
else:
    sys.exit(-1)
