# Change Log

## [X.X.X.X]

### Enhancements

#### Build

- **20.08 Support**: Building for USD 20.08 is supported.

#### Procedural

- **UsdRender schema**: UsdRenderSetting, UsdRenderProduct and UsdRenderVar are now supported in the procedural. (#453)

#### Render Delegate

- **Improved Solaris Primvar support**: Primvars with arrays of a single element are converted to non-array user data in Arnold. This improves primvar support in Houdini Solaris. (#456)
- **Basis Curves**: The Render Delegate now supports rendering of Basis Curves. Overriding the basis parameter via primvars and periodic/pinned wrapping are not supported. (#19)
- **Per instance primvars**: Instancer primvars are now supported, with the exception of nested instancer primvars. (#478)
- **Overriding the default filter**: HDARNOLD_default_filter and HDARNOLD_default_filter_attributes can now be used to overwrite the default filter. (#475)

### Bugfixes

#### Procedural

- #463 Texture coordinates of texcoord2f type are not read correctly

#### Render Delegate

- #475 The closest filter is used for AOVs without filtering information

## [6.0.3.1]

### Build

- **Testing the Scene Format:** The testsuite now includes tests for the Scene format plugin. (#157)
- **Hiding symbols:** Weak symbols are now hidden in the Procedural on Linux and MacOS. (#409)
- **Lambert for the Tests:** The testsuite is now using the lambert as the default shader. (#416)
- **Overwriting the C++ standard:** The C++ standard can now be overwritten for builds on Linux and MacOS. (#446)
- **Google Test:** Google Test can now be used to write tests. (#311)

- Fixed an issue with the "resave" test parameter. (#402)
- Fixed an issue with test 62 and 68. (#414)

### Procedural

- **UsdGeomCamera:** The UsdGeomCamera schema is now supported in the procedural. (#345)
- **UsdSkel:** The UsdSkel schema is now supported in the procedural. (#329)
- **Image uvset:** The built-in uvset is used now when a UsdPrimvarReader for st/uv is connected to UsdUvTexture. (#428)
- **Textured Mesh Lights:** Textured mesh lights are now supported. (#366)

- Fixed an issue when using multiple Procedurals. (#400)
- Fixed issues with Nested Procedurals when using the Procedural Viewport API. (#408 #435)
- Fixed a crash with empty node names. (#380)
- Fixed an issue with reading namespaced primvars. (#382)
- Fixed an issue when reading Light color and intensity parameters. (#364)
- Fixed several issues when reading primvars. (#333)
- Fixed an issue when reading RGB arrays. (#325)

### Render Delegate

- **Filters for AOVs:** Filtering parameters are now read from the aovSettings map for RenderVars. (#319 #426 #437)
- **LPE RenderVars:** LPE RenderVars are now supported. (#317)
- **Primvar RenderVars:** Primvar RenderVars are now supported. (#318)
- **SourceName for RenderVars:** The sourceName aovSetting is now supported. This allows renaming Arnold AOVs and writing a single AOV to multiple Render Buffers with different filters. (#425)

- Fixed an issue that prevented using the GPU on Windows. (#398)
- Fixed a crash when running the Render Delegate in Solaris on Windows. (#394)
- Fixed a bug related to marking ignored Render Buffers as converged. (#431)
- Fixed an issue related to using incorrect LPEs. (#430)
- Fixed a crash when deleting an active RenderVar in Solaris. (#439)
- Fixed an issue when the USD Preview Surface is used as a Displacement shader. (#448)
- Fixed an issue with how the dataType parameter is interpreted. (#450)

### Scene Format / Writer

- **Roundtripping Node Names:** Node names are now preserved when roundtripping scenes from Arnold to USD to Arnold. (#396)
- **Per-channel connections:** Per-channel connections are now written using adapter nodes. (#351)
- **Motion Keys:** Motion keys are now written to the USD file. (#334)
- **Motion Blur:** Motion blur is now supported. (#346)

- Fixed an issue when writing Toon light lists. (#374)
- Fixed an issue when writing linked ramp parameters. (#375)
- Fixed an issue when writing AI_TYPE_NODE user data. (#371)
- Fixed an issue when writing motion ranges. (#368)
- Fixed an issue when writing ginstance parameters. (#362)
- Fixed a crash when writing empty arrays. (#360)
- Fixed an issue when the number of motion keys for normals did not match the number of motion keys for positions. (#356)
- Fixed an issue when writing custom Matrix parameters. (#354)
- Fixed an issue when writing polymesh.subdiv_iterations.(#349)
- Fixed an issue when writing curves.num_points. (#324)
- Removed warnings when writing the displayColor primvar. (#312)

## [6.0.3.0]

### Build

- **USD requirement:** USD 19.05 is now the earliest supported version.
- **USD 20.02:** USD 20.02 is now supported. (#260)

- Fixed build issues on Linux and MacOS. (#303)
- Fixed build issues when using USD 0.19.05.

### Procedural

- **Using the new instancer procedural:** The procedural now uses the core shipped instancer procedural. (#256)
- **Prim Visibility:** The visibility token from UsdGeomImageable is now correctly inherited. (#218)

- Fixed a crash when using the Arnold Viewport functions. (#295)
- Fixed a crash when writing USD files with upper-case extensions. (#288)

### Render Delegate

- **Computed Primvars:** Computed primvars are now supported, enabling previewing UsdSkel and Houdini crowds. (#265 and #267)
- **Improved Rendering:** The Render Delegate is now using the Arnold Render API correctly, leading to better responsiveness. (#270)
- **Improved Render Buffers:** The Hydra Render Buffer support is now significantly improved, including improved performance. (#8)
- **32-bit buffers:** The render delegate now outputs 32-bit float buffers, instead of dithered 8 bit whenever possible. (#9)
- **arnold:global: prefix:** Prefixing Render Settings with `arnold:global:` is now supported.
- **Shaping Parameters:** Shaping parameters on Sphere Lights are now supported. This includes Spot and IES parameters, excluding IES normalize and IES angle scale. (#314)
- **Barndoor Parameters:** Solaris' Barndoor parameters are now roughly approximated using the barndoor filter. Note, Arnold *does NOT match* Karma. (#332)

- Fixed a crash when instancer nodes had uninitialized node pointers. (#298)
- Fixed a bug with aborted renders not marking the render pass as converged. (#4)
- Fixed a bug where the camera jumped to the origin in Solaris. (#385)

### Scene Format/Writer

- **Mask:** The scene format plugin now supports a mask parameter, allowing selective export of Arnold nodes. (#274)
- **Closures:** Closure attributes are now written to the USD file. (#322)
- **Options:** The options node is now correctly translated. (#320)

- Fixed bugs with the string export functions. (#320)
- Fixed a crash when writing pointer attributes. (#342)

## [6.0.2.1]

### Build

- **Clang 9:** We now support building using Clang 9. (#252)

### Procedural

- **Primvar translation:** The procedural now uses "primvars:st" and "primvars:uv" for UV coordinates, and "normals" for "nlist" on polymeshes. (#206)
- **Per-vertex UV and Normals:** Per-vertex UVs and normals are now supported on polymeshes. (#222)
- **Shader output connections:** Support for shader output connections has been improved, including per-component connections. (#211)
- **Point varying primvars:** Varying primvars are now supported on point schemas. (#228)
- **USDVol support:** The procedural now supports USD Volumes schemas. Using grids from two different files for a single volume is not allowed. (#227)
- **Serialized USD:** The procedural supports creating the stage from a set of strings without requiring a file. (#235)
- **Basis Curves:** The procedural now supports Basis Curves. (#239)
- **Boolean parameters:** The procedural now supports setting boolean parameters on Arnold nodes using bool, int or long USD attributes. (#238)
- **Converting between Width and Radius:** Width attributes are now properly converted to Arnold's radius parameters. (#247)
- **RTLD_LOCAL:** RTLD_LOCAL is used instead of RTLD_GLOBAL when dlopening in the procedural. (#259)

### Render Delegate

- **Left Handed Topology:** Left-handed topologies are now supported, including converting the varying primvars and indices for position, normal and uv attributes. (#207)
- **Motion Key mismatch:** Normal and position motion keys counts are the same now. (#229)
- **RGBA Primvars:** RGBA primvars are now supported. (#236)
- **Hydra Point Instancer:** Support of the Point Instancer has been greatly improved, including instancing of volumes. (#18 and #123)
- **Uniform Primvars:** Uniform primvars on points are now supported.
- **Pixel format conversion:** The render buffers now converts between pixel formats correctly. (#245)
- **Light Dirtying:** Transforms are now correctly preserved when parameters are dirtied, but transforms are unchanged on lights. (#243)

### Schemas

- **Linux Build:** Building schemas using Arnold 6.0.2.0 is now supported on Linux. (#219)

### Writer 

- **Node names for attributes:** Node names are now correctly formatted and sanitized when writing node and node array attributes. (#208)
