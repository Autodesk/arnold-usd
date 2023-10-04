import os
import sys

arnold_plugin = os.environ['ARNOLD_PLUGIN_PATH']

def find_in_file(expectedTypes, filename):

    file = open(filename, 'r')
    lines = file.readlines()
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
if not find_in_file(expectedSchemas, filename):
    sys.exit(-1)

filename = os.path.join(arnold_plugin, 'usd', 'usdArnold', 'resources', 'generatedSchema.usda')
if not find_in_file(expectedSchemas, filename):
    sys.exit(-1)

expectedNdr = ["NdrArnoldParserPlugin", "NdrArnoldDiscoveryPlugin"]
filename = os.path.join(arnold_plugin, 'usd', 'ndrArnold', 'resources', 'plugInfo.json')
if not find_in_file(expectedNdr, filename):
    sys.exit(-1)

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
if not find_in_file(expectedImaging, filename):
    sys.exit(-1)

print('SUCCESS')