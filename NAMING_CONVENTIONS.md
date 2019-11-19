Arnold USD naming conventions
=======================

The Arnold USD implementation supports arnold-specific attributes through custom schemas.

## Arnold attributes on USD nodes

Additional arnold attributes on USD-native nodes (geometries, lights, etc...) need to be in the "arnold" namespace

Example:
def DomeLight "my_light"
{
 float intensity=2
 int arnold:max_bounces=99
}

## Arnold Shaders

Arnold shaders are supported with UsdShade Shader primitives. The info:id attribute should be the shader name, with an "arnold" namespace. The attributes will be in the "inputs" scope.

Example:
def Shader "my_shader"
{
    uniform token info:id = "arnold:standard_surface"
    color3f inputs:base_color = (0,0,1)
}

## Arnold nodes

Any Arnold node can be represented in USD, with a camel-cased node type, prefixed with "Arnold". 
Example:
def ArnoldStandardSurface "my_shader" {}
def ArnoldSetOperator "my_operator" {}
def ArnoldPolymesh "my_mesh" {}
def ArnoldSkydomeLight "my_light" {}

For these nodes, the attributes don't need to be namespaced.