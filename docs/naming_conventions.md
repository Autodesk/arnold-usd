Arnold USD Naming Conventions
=======================

In order to expose the entirety of the Arnold features in USD, we supply custom schemas for Arnold primitives in USD, and we support Arnold-specific attributes on regular USD prims. This documents the naming conventions in use to map Arnold node and parameters names in USD.

## Arnold attributes on USD prims

Additional Arnold attributes on native USD prims (geometry, lights, etc.) need to be set as primvars in the `arnold` namespace:

```
def DomeLight "my_light"
{
 float intensity=2
 int primvars:arnold:max_bounces=99
}
```

Primvars will be converted as user data on the Arnold nodes, at the exception of those in the arnold: scope that will be translated as built-in attributes. 
In the above example "int primvars:arnold:max_bounces = 99" will be equivalent to "int arnold:max_bounces=99"


## Arnold Shaders

Arnold shaders are supported as UsdShade Shader prims. The `info:id` attribute should be the Arnold shader name, prefixed by the `arnold` namespace. The shader parameter are placed in the `inputs` scope.

```
def Shader "my_shader"
{
    uniform token info:id = "arnold:standard_surface"
    color3f inputs:base_color = (0,0,1)
}
```

For more details about UsdShadeShader schemas, see https://graphics.pixar.com/usd/docs/api/usd_shade_page_front.html

## Arnold Nodes

Any Arnold node can be represented in USD, with a camel-cased node type, prefixed with "Arnold":

```
def ArnoldStandardSurface "my_shader" {}
def ArnoldSetOperator "my_operator" {}
def ArnoldPolymesh "my_mesh" {}
def ArnoldSkydomeLight "my_light" {}
```

For these nodes, the attribute names are the Arnold parameter names and are not namespaced.