Arnold USD
==========

This repository contains a set of components and tools to use the [Arnold](https://www.arnoldrenderer.com) renderer with Pixar's [Universal Scene Description](https://github.com/PixarAnimationStudios/USD). Notably, the following components are included:

- Hydra render delegate
- Arnold procedural for USD
- Schemas to describe an Arnold scene in USD

Contributions are welcome! Please make sure to read the [contribution guidelines](CONTRIBUTING.md).


## Building and installation

Please follow the [building instructions](docs/building.md). To use the components, provided you installed in `<arnold-usd_dir>`, set the following environment variables:

- Add `<arnold-usd_dir>/procedural` to `ARNOLD_PLUGIN_PATH` for the Arnold `usd` procedural.
- Add `<arnold-usd_dir>/lib/python` to `PYTHONPATH` for the Python schema bindings.
- Add `<arnold-usd_dir>/plugin` to `PXR_PLUGINPATH_NAME` for the Hydra render delegate and the Node Registry plugin.
- Add `<arnold-usd_dir>/lib/usd` to `PXR_PLUGINPATH_NAME` for the USD schemas.
- Add `<arnold-usd_dir>/lib` to `LD_LIBRARY_PATH` on Linux, `PATH` on Windows and `DYLD_LIBRARY_PATH` on Mac.


## Hydra Render Delegate

The render delegate currently supports the following features:

- RPrim Support
    - Mesh
        - All primvars are supported, st/uv is accessible through the built-in uv attribute on the mesh
        - Normal primvars are translated to Arnold built-in attributes
        - Support for the displayColor primvar
        - Subdivision settings
    - Volume
    - Points
- SPrim Support
    - Materials
        - Arnold shaders are supported, the `info:id` attribute is used to determine the shader type
        - UsdPreviewSurface is translated to Arnold shaders
    - Lights
        - Distant Light
        - Sphere Light
        - Disk Light
        - Rect Light
        - Cylinder Light
        - Dome Light
- BPrim Support
    - Render Buffer
    - OpenVDB Asset
- Point Instancer, including nesting of Point Instancers and instancing Volumes
- Selection in USD View and other applications using the `primId` AOV
- Displaying the Color, Depth and PrimID AOVs
- Motion Blur
    - Deformation
    - Transformation
- USD Skel support
- HdExtComputation support for polymeshes
- Render Settings via the Render Delegate
    - Sampling parameters
    - Threading parameters
    - Ignore parameters
    - Profiling and logging parameters
    - Switching between CPU and GPU mode seamlessly
    - Default values are configurable through environment variables for most of these parameters

**Limitations**
- Crash on linux at startup with usdview : Currently, the arnold library needs to be ld-preloaded to avoid the crash, e.g. `LD_PRELOAD=/path_to_arnold/bin/libai.so usdview scene.usda`
- No motion blur support for the Point Instancer attributes
- Can’t preview arbitrary primvar AOVs
- No basisCurves
- No field3d volume grids
- Not all the parameters are accessible through the render settings
    - Texture Cache size
    - Texture generation parameters (automip, autotile)
- No normal maps on the UsdPreviewSurface
- Only converging renders are supported (ie. it’s not possible to block the viewport until the render finishes)
- No of physical camera parameters
- No coordsys support
- No per face material assigments
- Can't open textures from usdz files
- Only 8 bit precision for the rendered buffers

## Node Registry Plugin

The Node Registry plugin supports the current features:
- Registering Sdr nodes for every built-in shader and custom shader
    - Setting up the asset URI either to `<built-in>` or to the path of the shader library providing the shader.
    - Creating all parameters.

**Limitations**
- No nodes registered for
    - Shapes
    - Lights
    - Filters
    - Drivers
- No node is registered for the options node
- Metadata is not converted for any node

## Arnold USD Procedural

The procedural supports the following features:

- USD shapes
    - UsdGeomMesh
    - UsdGeomCurves
    - UsdGeomBasisCurves
    - UsdGeomPoints
    - UsdGeomCube
    - UsdGeomSphere
    - UsdGeomCone
    - UsdGeomCylinder
    - UsdPointInstancer
    - UsdVolume
    - primvars are translated as user data
- USD Lights
    - UsdLuxDistantLight
    - UsdLuxDomeLight
    - UsdLuxDiskLight
    - UsdLuxSphereLight
    - UsdLuxRectLight
    - UsdLuxGeometryLight
    - Support for textured lights (dome and rectangle)
- USD native shaders
    - UsdPreviewSurface
    - UsdPrimVar*
    - UsdUVTexture
- Arnold shaders supported as UsdShade nodes (where info:id gives the shader type)
- Support for any additional Arnold parameter in USD nodes (e.g. attribute `primvars:arnold:subdiv_iterations` in a UsdGeomMesh)
- Support for any Arnold node type (e.g. USD type ArnoldSetParameter gets rendered as arnold `set_parameter` node)
- Support for multi-threaded parsing of a USD file

**Limitations**
Currently unsupported:
- Nurbs
- Cameras
- Connections to input attribute channels

## Testsuite
Running the arnold-usd testsuite requires the latest version or Arnold, that can be downloaded in 
https://www.arnoldrenderer.com/arnold/download/
It is not supported for older versions of Arnold.

## Acknowledgments

- Luma Pictures' [usd-arnold](https://github.com/LumaPictures/usd-arnold)
- RodeoFX's [OpenWalter](https://github.com/rodeofx/OpenWalter)
- Ben Asher
- Sebastien Blaineau-Ortega
- Chad Dombrova
- Guillaume Laforge
- Julian Hodgson
- Thiago Ize
- Pal Mezei
- Paul Molodowitch
- Nathan Rusch
- Frederic Servant
- Charles Flèche
