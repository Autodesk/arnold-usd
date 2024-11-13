import os
import sys

arnold_plugin_paths = os.environ['ARNOLD_PLUGIN_PATH']
usd_plugin_paths = os.environ['PXR_PLUGINPATH_NAME']

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
                    'ArnoldBox', 
                    'ArnoldBoxAPI',
                    'ArnoldCameraAPI', 
                    'ArnoldColorManagerAPI',
                    'ArnoldCone', 
                    'ArnoldConeAPI',
                    'ArnoldCurvesAPI',
                    'ArnoldCylinder',
                    'ArnoldCylinderAPI',
                    'ArnoldDisk', 
                    'ArnoldDiskLightAPI',
                    'ArnoldDistantLightAPI',
                    'ArnoldGinstance',
                    'ArnoldImplicit', 
                    'ArnoldInstancer',
                    'ArnoldLightAPI',
                    'ArnoldMeshLightAPI',
                    'ArnoldNodeGraph',
                    'ArnoldNurbs', 
                    'ArnoldOptions',
                    'ArnoldOptionsAPI',
                    'ArnoldOrthoCameraAPI',
                    'ArnoldPerspCameraAPI',
                    'ArnoldPlane', 
                    'ArnoldPointLightAPI',
                    'ArnoldPointsAPI',
                    'ArnoldPolymeshAPI',
                    'ArnoldProcedural',
                    'ArnoldProceduralCustom',
                    'ArnoldQuadLightAPI',
                    'ArnoldShapeAPI',
                    'ArnoldSkydomeLightAPI',
                    'ArnoldSphere',
                    'ArnoldSphereAPI',    
                    'ArnoldUsd',                
                    'ArnoldUsdLuxLightFilter',
                    'ArnoldVolume',
                    'ArnoldVolumeImplicit']

    filename = os.path.join(arnold_plugin, 'usdArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        print('schemas file not found {}'.format(filename))
        return False
    if not find_in_file(expectedSchemas, filename):
        print('expected schemas not found in {}'.format(filename))
        return False

    print('Expected schemas were found in {}'.format(filename))

    filename = os.path.join(arnold_plugin, 'usdArnold', 'resources', 'generatedSchema.usda')
    if not os.path.exists(filename):
        print('schemas file not found {}'.format(filename))
        return False
    if not find_in_file(expectedSchemas, filename):
        print('expected schemas not found in {}'.format(filename))
        return False

    print('Expected schemas were found in {}'.format(filename))
    return True

def test_ndr(arnold_plugin):
    expectedNdr = ["NdrArnoldParserPlugin", "NdrArnoldDiscoveryPlugin"]
    filename = os.path.join(arnold_plugin, 'ndrArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        print('ndr file not found {}'.format(filename))
        return False
    if not find_in_file(expectedNdr, filename):
        print('expected ndr not found in {}'.format(filename))
        return False

    print('Expected ndr were found in {}'.format(filename))
    return True

def test_imaging(arnold_plugin):
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
                        
    filename = os.path.join(arnold_plugin, 'usdImagingArnold', 'resources', 'plugInfo.json')
    if not os.path.exists(filename):
        print('imaging file not found {}'.format(filename))
        return False
    if not find_in_file(expectedImaging, filename):
        print('Expected imaging not found in {}'.format(filename))
        return False

    print('Expected imaging were found in {}'.format(filename))
    return True

arnold_plugin_paths = arnold_plugin_paths.split(os.pathsep)
arnold_plugin_paths = [os.path.join(path, 'usd') for path in arnold_plugin_paths]
usd_plugin_paths = usd_plugin_paths.split(os.pathsep)

found_schemas = False
found_ndr = False
found_imaging = False
for arnold_plugin in arnold_plugin_paths + usd_plugin_paths:
    found_schemas = found_schemas or test_schemas(arnold_plugin)
    found_ndr = found_ndr or test_ndr(arnold_plugin)
    found_imaging = found_imaging or test_imaging(arnold_plugin)

if found_schemas and found_ndr and found_imaging:
    print('SUCCESS')
else:
    if not found_schemas:
        print("ERROR test_0245: usdArnold not found or incomplete")
    if not found_ndr:
        print("ERROR test_0245: ndrArnold not found or incomplete")
    if not found_imaging:
        print("ERROR test_0245: usdImagingArnold not found or incomplete")
    sys.exit(-1)
