Arnold USD Toolset
=======================

Arnold USD is a toolset for [Universal Scene Description](https://github.com/PixarAnimationStudios/USD) . The project contains several tools:

- Arnold Procedural
- Arnold to USD converter command line executable
- Hydra Render Delegate
- Set of USD schemas to describe an Arnold scene using USD

# Building and Dependencies

[Building](docs/building.md)

# Contributing to the Project

[Contributing](CONTRIBUTING.md)

# Setting up environment

After compiling and installing the project

- Add `<prefix>/bin` to PATH for the Arnold to USD writer executable.
- Add `<prefix>/lib/python` to PYTHONPATH for the python schema bindings.
- Add `<prefix>/plugin` to PXR_PLUGINPATH_NAME for the Hydra Render Delegate.
- Add `<prefix>/lib/usd` to the PXR_PLUGINPATH_NAME for the USD Schemas.
- Add `<prefix>/lib` to LD_LIBRARY_PATH on Linux, PATH on Windows and DYLD_LIBRARY_PATH on Mac.

# Features and Limitations

## Procedural

### Features

- USD shapes : UsdGeomMesh, UsdGeomCurves, UsdGeomPoints, UsdGeomCube, UsdGeomSphere, UsdGeomCone, UsdGeomCylinder. Support for primvars (user data)
- USD Lights : UsdLuxDistantLight, UsdLuxDomeLight, UsdLuxDiskLight, UsdLuxSphereLight, UsdLuxRectLight, UsdLuxGeometryLight.Support for textured lights (dome and rectangle).
- USD native shaders : UsdPreviewSurface, UsdPrimVar*, UsdUVTexture
- Arnold shaders supported as UsdShade nodes (where info:id gives the shader type)
- Support for any additional arnold parameter in USD nodes (ex: attribute arnold:subdiv_iterations in a UsdGeomMesh)
- Support for any arnold node type (ex: Usd type ArnoldSetParameter gets rendered as arnold set_parameter node)
- Support for multi-threaded parsing of a USD file

### Limitations

- Nurbs
- Point Instancer
- Cameras

## Render Delegate

### Features
- RPrim Support
  - Mesh
    - All primvars are supported, st/uv is accessible through the built-in uv attribute on the mesh
    - Support for the displayColor primvar
    - Subdivision settings
  - Volume
- SPrim Support
  - Material
     - Pixar Preview surfaces are translated to arnold nodes, otherwise the info:id attribute is used to determine the shader type
  - Distant Light
  - Sphere Light
  - Disk Light
  - Rect Light
  - Cylinder Light
  - Dome Light
- BPrim Support
  - Render Buffer
  - OpenVDB Asset
- Point Instancer
  - Including nesting of Point Instancers
- Selection in USD View and other applications using the primId aov
- Displaying the Color, Depth and PrimID AOVs
- Motion Blur
  - Deformation
  - Transformation
- Render Settings via the Render Delegate
  - Sampling parameters
  - Threading parameters
  - Ignore parameters
  - Profiling and logging parameters
  - Switching between CPU and GPU mode seamlessly
  - Default values are configurable through environment variables for most of these parameters

### Limitations

- No motion blur support for the Point Instancer attributes
- Can’t preview arbitrary primvar AOVs
- No basisCurves
- No field3d volume grids
  - Not all the parameters are accessible through the render settings
  - Texture Cache size
- Texture generation parameters (automip, autotile)
- No normal maps on the UsdPreviewSurface
- Only converging renders are supported (ie. it’s not possible to block the viewport until the render finishes)
- No HdExtComputation and UsdSkel computation via the render delegate
- No of physical camera parameters
- No points
- No coordsys support

### Acknowledgments

In alphabetical order:

- Ben Asher
- Chad Dombrova
- Nathan Rusch
- Paul Molodowitch
