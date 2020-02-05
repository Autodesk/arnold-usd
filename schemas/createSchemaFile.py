# Copyright 2019 Autodesk, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
## load our own python modules
import os
import platform
import sys
import filecmp
import arnold as ai

if len(sys.argv) < 2:
    print 'Not enough arguments!'
    sys.exit(1)

def getParameterStr(paramType, paramValue = None, paramEntry = None):
    typeStr = ''
    valueStr = ''
    optionsStr = ''
    if paramType == ai.AI_TYPE_BYTE:
        typeStr = 'uchar'
        if paramValue:
            valueStr = str(paramValue.contents.BYTE)
    elif paramType == ai.AI_TYPE_INT:
        typeStr = 'int'
        if paramValue:
            valueStr = str(paramValue.contents.INT)
    elif paramType ==  ai.AI_TYPE_UINT:
        typeStr = 'uint'
        if paramValue:
            valueStr = str(paramValue.contents.UINT)
    elif paramType ==  ai.AI_TYPE_BOOLEAN:
        typeStr = 'bool'
        if paramValue:
            valueStr = 'true' if paramValue.contents.BOOL else 'false'
    elif paramType ==  ai.AI_TYPE_FLOAT:
        typeStr = 'float'
        if paramValue:
            valueStr = str(paramValue.contents.FLT)
    elif paramType ==  ai.AI_TYPE_RGB:
        typeStr = 'color3f'
        if paramValue:
            rgbVal = paramValue.contents.RGB
            valueStr = '({},{},{})'.format(str(rgbVal.r), str(rgbVal.g), str(rgbVal.b))
    elif paramType ==  ai.AI_TYPE_RGBA:
        typeStr = 'color4f'
        if paramValue:
            rgbaVal = paramValue.contents.RGBA
            valueStr = '({},{},{},{})'.format(str(rgbaVal.r), str(rgbaVal.g), str(rgbaVal.b), str(rgbaVal.a))
    elif paramType ==  ai.AI_TYPE_VECTOR:
        typeStr = 'vector3f'   # vector3f
        if paramValue:
            vecVal = paramValue.contents.VEC
            valueStr = '({},{},{})'.format(str(vecVal.x), str(vecVal.y), str(vecVal.z))
    elif paramType ==  ai.AI_TYPE_VECTOR2:
        typeStr = 'float2'
        if paramValue:
            vec2Val = paramValue.contents.VEC2
            valueStr = '({},{})'.format(str(vec2Val.x), str(vec2Val.y))
    elif paramType ==  ai.AI_TYPE_STRING:
        typeStr = 'string'
        if paramValue:
            strVal = str(paramValue.contents.STR)
            valueStr = '"{}"'.format(str(strVal))

    # FIXME: matrix parameter is an array
    elif paramType ==  ai.AI_TYPE_MATRIX:
        typeStr = 'matrix4d'
        if paramValue:
            if paramValue.contents.pMTX is None:
                return '', '', ''
            mtxVal = paramValue.contents.pMTX[0]
            valueStr = '(({},{},{},{}),({},{},{},{}),({},{},{},{}),({},{},{},{}))'.format(mtxVal[0][0], mtxVal[0][1], mtxVal[0][2], mtxVal[0][3], mtxVal[1][0],\
                mtxVal[1][1],mtxVal[1][2],mtxVal[1][3],\
                mtxVal[2][0], mtxVal[2][1], mtxVal[2][2],\
                mtxVal[2][3], mtxVal[3][0],mtxVal[3][1],\
                mtxVal[3][2],mtxVal[3][3])
    elif paramType ==  ai.AI_TYPE_ENUM:
        typeStr = 'token'
        paramEnum = ai.AiParamGetEnum(paramEntry)
        if paramValue:
            intVal = paramValue.contents.INT
            strVal = str(ai.AiEnumGetString(paramEnum, intVal))
            
            # FIXME we still have a problem with parameter "auto" as it creates a token that can't be parsed properly in the generated file tokens.h
            if strVal == 'auto':
                strVal = 'metric_auto'
            
            valueStr = '"{}"'.format(strVal)
        optionsStr = '\n        allowedTokens = ['

        i = 0
        while True:
            t = ai.AiEnumGetString(paramEnum, i)
            if t is None:
                break

            if t == 'auto':
                t = "metric_auto"
            
            if i > 0:
                optionsStr += ','
            optionsStr += '"{}"'.format(t)
            i += 1
        optionsStr += ']'
        
    elif paramType ==  ai.AI_TYPE_CLOSURE: # shouldn't be needed since closures are just for shaders
        typeStr = 'color4f'
        valueStr = '(0,0,0,0)'

    return typeStr, valueStr, optionsStr

''' 
Convert an Arnold Param Entry to a USD declaration
and return it as a string
'''
def arnoldToUsdParamString(paramEntry, scope):
    ret = ''
    paramName = ai.AiParamGetName(paramEntry)
    if paramName == 'name':
        return '' # nothing to return for attribute 'name'

    # Add the arnold scope to the attribute namespace, so that the token can be compiled
    if paramName == 'namespace' and len(scope) == 0:
        scope = 'arnold:'

    optionsStr = "customData = {string apiName = \"arnold_%s\"}" % paramName
    if len(paramName) == 1:
        paramName = paramName.lower()


    '''
    // TODO:
    // AI_TYPE_POINTER
    // AI_TYPE_NODE
    // AI_TYPE_USHORT
    // AI_TYPE_HALF
    // AI_TYPE_UNDEFINED
    // AI_TYPE_NONE
    '''
    paramType = ai.AiParamGetType(paramEntry)
    paramDefault = ai.AiParamGetDefault(paramEntry)
    paramVal = ''
    optionsStr = ''

    if paramType != ai.AI_TYPE_ARRAY:
        typeStr, valueStr, optionsValStr = getParameterStr(paramType, paramDefault, paramEntry)
        if typeStr is None or typeStr == '':
            return ''

        ret += typeStr
        paramVal = valueStr
        optionsStr += optionsValStr
        ret += ' {}{} = {} ({})'.format(scope, paramName, paramVal, optionsStr)

    else:
        # Array parameter
        arrayVal = paramDefault.contents.ARRAY
        if arrayVal is None or arrayVal[0] is None:
            return ''
        arrayVal = arrayVal[0]
        arrayType = ai.AiArrayGetType(arrayVal)
        typeStr, valueStr, optionsValStr = getParameterStr(arrayType)
        if typeStr is None or typeStr == '':
            return ''

        ret += '{}[] {}{}'.format(typeStr, scope, paramName)

    # FIXME do we need to set the API name ?
    # ret += ' (customData = {string apiName = "arnold{}"}\n'.format(GetCamelCase(paramName))
    return ret

def makeCamelCase(name):
    return ''.join(x.capitalize() or '_' for x in name.split('_'))

'''
Let's create the file schema.usda
and add the headers
'''
if not os.path.exists(sys.argv[1]):
    os.makedirs(sys.argv[1])
schemaFile = os.path.join(sys.argv[1], 'schema.usda')
schemaPreviousFile = '{}.bak'.format(schemaFile)
if os.path.exists(schemaPreviousFile):
    os.remove(schemaPreviousFile)

# backup the current schema.usda file, to see if it changed
if os.path.exists(schemaFile):
    os.rename(schemaFile, schemaPreviousFile)    

# This is typically named 'module.cpp' in USD modules, but we name it
# 'wrapModule.cpp', so every python module related file has the name 'wrap'.

file_module = open(os.path.join(sys.argv[1], 'wrapModule.cpp'), 'w')

file_module.write(
'''#include <pxr/pxr.h>
#include <pxr/base/tf/pyModule.h>

PXR_NAMESPACE_USING_DIRECTIVE

TF_WRAP_MODULE
{
    TF_WRAP(UsdArnoldTokens);
'''
)

file = open(schemaFile, 'w')
file.write(
'''#usda 1.0
(
"This file describes the Arnold USD schemas."
    subLayers = [
        @usd/schema.usda@,
        @usdGeom/schema.usda@,
        @usdShade/schema.usda@,
        @usdLux/schema.usda@
    ]
)
over "GLOBAL" (
    customData = {
        string libraryName = "usdArnold"
        string libraryPath = "./"
        string libraryPrefix = "Usd"
        string tokensPrefix = "UsdArnold"
    }
) {
}
'''
)

def createArnoldClass(entryName, parentClass, paramList, nentry, parentParamList = None, isAPI = False):

    schemaName = 'Arnold{}'.format(makeCamelCase(entryName))
    attrScope = ''

    if isAPI:
        file.write('class "{}API"(\n'.format(schemaName))
        parentClass = 'APISchemaBase'
        attrScope = 'primvars:arnold:' # need to primvars with the arnold namespace for USD-native nodes
    else:
        file.write('class {} "{}"(\n'.format(schemaName, schemaName))
    
    file.write('    inherits = [</{}>]\n'.format(parentClass))
    file.write(') {\n')
        
    for param in paramList:
        if param == 'name':
            continue  # nothing to do with attribute 'name'
        
        if parentParamList and param in parentParamList: # if this parameter already exists in the parent class we don't add it here
            continue  

        paramEntry = ai.AiNodeEntryLookUpParameter(nentry, param)
        if paramEntry == None:
            print 'Param Entry not found: {}.{}'.format(entryName, param)
            continue

        paramStr = arnoldToUsdParamString(paramEntry, attrScope)
        if paramStr != None and len(paramStr) > 0:
            file.write('    {}\n'.format(paramStr))
   
    file.write('}\n')

    if isAPI:
        file_module.write('    TF_WRAP(Usd%sAPI);\n' % schemaName)
    else:
        file_module.write('    TF_WRAP(Usd%s);\n' % schemaName)

        
'''
Here we want to generate a file schema.usda containing all the arnold node entries as new schemas
We will then run usdGenSchema schema.usda in order to generate all the c++ schema classes.
After that we'll need to compile these classes in a library that will have to be accessed by USD to know arnold schemas

The Arnold schemas will be named ArnoldPolymesh, ArnoldStandardSurface, etc...
Instead of having lots of separate schemas, we're going to group them by type. 
Thus each arnold node entry type (shape, shader, operator, etc...) will have a corresponding schema with the common parameters.
The schemas for each node entry will then derive from their "type" schema

For example, we want a schema "ArnoldLight" defining the parameters
"color", "intensity", "exposure", etc...
and then "ArnoldSkydomeLight" deriving from "ArnoldLight" and defining the additional skydome parameters.

That's not a simple information to get from Arnold as there's no API to get info about a node entry type.
So below we're going to list the arnold node entries, check their type and list of parameters. 
For example, in order to get the list of parameters for "ArnoldLight", we're going to compute the union of 
all parameters for in "ArnoldSkydomeLight", "ArnoldDistantLight", "ArnoldPointLight", "ArnoldQuadLight", etc...
In theory we should also compare the parameter types and their default values, from AFAIK we shouldn't have this problem in the current set of arnold nodes
'''
ai.AiBegin()

entryList = [] # list of [nodeEntryName, nodeEntryTypeName, parametersList ]
entryByType = {} # dictionary (key= nodeEntryTypeName, value=list of nodeEntryName for this type)
typeParams = {} # dictionary (key=nodeEntryTypeName, value= list of parameters which exist in ALL the corresponding nodeEntryName)

# Loop over node entries
nodeEntryIter = ai.AiUniverseGetNodeEntryIterator(ai.AI_NODE_ALL)
while not ai.AiNodeEntryIteratorFinished(nodeEntryIter):
    nentry = ai.AiNodeEntryIteratorGetNext(nodeEntryIter);

    # Name of this AtNodeEntry (distant_light, skydome_light, etc...)
    entryName = str(ai.AiNodeEntryGetName(nentry))
    # Type of this AtNodeEntry (light, shape, shader, operator, etc...)
    entryTypeName = str(ai.AiNodeEntryGetTypeName(nentry))

    # we don't want to create schemas for shaders, as we're relying on UsdShade schemas
    if entryTypeName == 'shader':
        continue
    
    # Get the list of parameters for this node entry
    paramsList = []

    # Loop over AtParamEntry for this AtNodeEntry
    paramsIter = ai.AiNodeEntryGetParamIterator(nentry)
    while not ai.AiParamIteratorFinished(paramsIter):
        paramEntry = ai.AiParamIteratorGetNext(paramsIter)
        # Add this parameter name to our list
        paramsList.append(ai.AiParamGetName(paramEntry))
    ai.AiParamIteratorDestroy(paramsIter) # iterators need to be deleted

    # Add an element in my list of node entries, with its name, type name, and list of parameters
    entryList.append([entryName, entryTypeName, paramsList])
    
    if entryByType.get(entryTypeName) is None:
        # first time we find a node with this type, let's make the dict value a list
        entryByType[entryTypeName] = []

    # Add this node entry to its "type" dictionary
    entryByType[entryTypeName].append(entryName)

    if typeParams.get(entryTypeName) is None:
        # first time we find a node with this type, let's simply copy all parameters for this node entry
        typeParams[entryTypeName] = paramsList
    else:
        # We want the final list to be the union between the existing list and paramsList
        # So first we copy the existing list for this type
        prevParamList = typeParams[entryTypeName]
        # We create a new list that we'll set afterwards
        newList = []
        for prevParam in prevParamList:
            # for each of the existing parameters, we check if it also exists in this node entry
            if prevParam in paramsList:
                # it also exists, let's add it back to our list
                newList.append(prevParam)
        # Setting the updated list of parameters back to our dictionary
        typeParams[entryTypeName] = newList

ai.AiNodeEntryIteratorDestroy(nodeEntryIter) # iterators need to be deleted

'''
Now let's create a new class for each "type", let it inherit from a meaningful schema, and let's add its parameters based on the existing dictionary
'''
for key, paramList in typeParams.iteritems():
    if key == 'options' or key == 'override':
        continue

    blacklistParams = []

    parentClass = 'Typed'
    if key == 'light':
        parentClass = 'Light'
        blacklistParams = ['intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix']
    elif key == 'shape':
        parentClass = 'Boundable' # FIXME should it be imageable ?
        blacklistParams = ['matrix']
    elif key == 'camera':
        parentClass = 'Camera'
    elif key == 'operator':
        parentClass = 'NodeGraph'

    entryNames = entryByType[key]  # list of entry names for this type
    if entryNames == None or len(entryNames) == 0:
        print 'This script is not working...no entries found for type {}'.format(key)

    entryName = entryNames[0] # using the first entry found here
    nentry = ai.AiNodeEntryLookUp(entryName)
    if nentry == None:
        print 'Hey I could not find any AtNodeEntry called {}'.format(entryName)
        continue

    createArnoldClass(key, parentClass, paramList, nentry, None, False)

    # Now Create the API schema for lights and shapes
    if key == 'light' or key == 'shape':
        createArnoldClass(key, 'APISchemaBase', paramList, nentry, blacklistParams, True)

# list of the node entries that need an API schema, along with the list of parameters to ignore
nativeUsdList = {
    'polymesh': ['nsides', 'vidxs', 'polygon_holes', 'nidxs', 'matrix', 'shader'],
    'curves' : ['num_points', 'points', 'radius', 'matrix', 'shader'],
    'points' : ['matrix', 'shader', 'points', 'radius'],
    'box' : ['min', 'max', 'matrix', 'shader'],
    'sphere' : ['radius', 'min', 'max', 'matrix', 'shader'],
    'cylinder' : ['radius', 'matrix', 'shader'],
    'cone' : ['bottom_radius', 'matrix', 'shader'],
    'distant_light' : ['angle', 'intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix'],
    'skydome_light' : ['filename', 'format', 'intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix'],
    'disk_light' : ['radius', 'intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix'],
    'quad_light' : ['vertices', 'filename', 'intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix'],
    'mesh_light' : ['mesh', 'intensity', 'color', 'exposure', 'diffuse', 'specular', 'normalize', 'matrix']
    }

for entry in entryList:    
    entryName = entry[0]
    entryTypeName = entry[1]
    parametersList = entry[2]
    nentry = ai.AiNodeEntryLookUp(entryName)
    if nentry == None:
        print 'I could not find any AtNodeEntry called {}'.format(entryName)
        continue

    parentClass = 'Arnold{}'.format(makeCamelCase(entryTypeName))
    if entryName == 'options' or entryName == 'override':
        continue # do we want to create schemas for the options ?

    createArnoldClass(entryName, parentClass, parametersList, nentry, typeParams[entryTypeName], False)

    if entryName in nativeUsdList:
        createArnoldClass(entryName, 'APISchemaBase', parametersList, nentry, nativeUsdList[entryName], True)

'''
Ok, all this was just for the "Typed" Schemas, where each arnold node type has a counterpart schema.
Now, we also want to have API schemas, so that people can get/set arnold attributes on a USD-native node type
'''

file_module.write('}\n')

# We're done with this file, we can close it now
file_module.close()
file.close()

# We're done with Arnold, let's end the session 
ai.AiEnd()
