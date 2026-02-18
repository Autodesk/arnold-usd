<!-- SPDX-License-Identifier: Apache-2.0 -->
# Changelog

## [7.4.5.1] (Unreleased)

### Bug Fixes

- [usd#2557](https://github.com/Autodesk/arnold-usd/issues/2557) - Fix regression when a geometry has an instancer without any instances
- [usd#2547](https://github.com/Autodesk/arnold-usd/issues/2547) - Changing material terminal is not updated with Hydra 2
- [usd#2536](https://github.com/Autodesk/arnold-usd/issues/2536) - ArnoldUsd procedural filename is not reported as an asset

## [7.4.5.0] 2026-02-10

### Features

- [usd#2521](https://github.com/Autodesk/arnold-usd/issues/2521) - Remove spurious warnings with cameras matrices
- [usd#2394](https://github.com/Autodesk/arnold-usd/issues/2394) - Support VDB fieldIndex
- [usd#2504](https://github.com/Autodesk/arnold-usd/issues/2504) - Ensure geom subsets don't get appended to usd in animated exports
- [usd#2496](https://github.com/Autodesk/arnold-usd/issues/2496) - Support arnold render output nodes when writing usd files
- [usd#2492](https://github.com/Autodesk/arnold-usd/issues/2492) - Fix path resolving issue for ocio config with absolute paths
- [usd#2459](https://github.com/Autodesk/arnold-usd/issues/2459) - Use lightweight instancing for geometries in the render delegate
- [usd#2469](https://github.com/Autodesk/arnold-usd/issues/2469) - Authored primvars should not have elementSize set to the array size
- [usd#2476](https://github.com/Autodesk/arnold-usd/issues/2476) - log_verbosity setting should affect both console and file logs 
- [usd#2471](https://github.com/Autodesk/arnold-usd/issues/2471) - Support for USD 25.11
- [usd#2478](https://github.com/Autodesk/arnold-usd/issues/2478) - Use the Z AOV for the hydra depth buffer

## [7.4.4.2] 2026-01-22

### Bug Fixes

- [usd#2509](https://github.com/Autodesk/arnold-usd/issues/2509) - Fix crash in Solaris when changing the rendered LOP interactively
- [usd#2514](https://github.com/Autodesk/arnold-usd/issues/2514) - Fix random inconsistencies between instantaneousShutter and ignore_motion_blur in hydra 


## [7.4.4.1] 2025-12-18

### Bug Fixes

- [usd#2501](https://github.com/Autodesk/arnold-usd/issues/2501) - Fix incorrect transforms and materials when authoring usd files with meshes parented under instances
- [usd#2467](https://github.com/Autodesk/arnold-usd/issues/2467) - Hidden primitives turned visible in IPR do not always show up in the render
- [usd#2486](https://github.com/Autodesk/arnold-usd/issues/2486) - Fix a crash in the scene index happening when the typename is not yet defined
- [usd#2472](https://github.com/Autodesk/arnold-usd/issues/2472) - Fix regression in default value of texture_auto_generate_tx in the render delegate plugin
- [usd#2481](https://github.com/Autodesk/arnold-usd/issues/2481) - Fix regression with camera motion blur in Solaris
- [usd#2478](https://github.com/Autodesk/arnold-usd/issues/2478) - Support using the Z AOV for the hydra depth buffer

## [7.4.4.0] 2025-11-12

### Features

- [usd#2418](https://github.com/Autodesk/arnold-usd/issues/2418) - Deprecate legacy parameters "threads", "debug" and "hydra" in the procedural
- [usd#1633](https://github.com/Autodesk/arnold-usd/issues/1633) - Ignore verbosity settings from USD files when rendering with "kick -v"
- [usd#2331](https://github.com/Autodesk/arnold-usd/issues/2331) - Support multiple AOVs with the same name in hydra render products
- [usd#2390](https://github.com/Autodesk/arnold-usd/issues/2390) - Add support for the MeshLightAPI
- [usd#2408](https://github.com/Autodesk/arnold-usd/issues/2408) - Fix render region calculation when based on windowNDC
- [usd#2404](https://github.com/Autodesk/arnold-usd/issues/2404) - Support asset_searchpath as a replacement for procedural & texture search paths.
- [usd#2407](https://github.com/Autodesk/arnold-usd/issues/2407) - Add support for RenderPass prim when using hydra2.
- [usd#2440](https://github.com/Autodesk/arnold-usd/issues/2440) - Support render settings resolution with windowNDC
- [usd#2436](https://github.com/Autodesk/arnold-usd/issues/2436) - Support cryptomatte with instances in hydra2
- [usd#2441](https://github.com/Autodesk/arnold-usd/issues/2441) - Fix invalid motion ranges caused by motion_start / motion_end attributes
- [usd#2452](https://github.com/Autodesk/arnold-usd/issues/2452) - Imager updates should not restart IPR renders
- [usd#2415](https://github.com/Autodesk/arnold-usd/issues/2415) - Incorrect transform when exporting complex object hierarchy to USD

### Bug Fixes

- [usd#2391](https://github.com/Autodesk/arnold-usd/issues/2391) Remove warnings when rendering with an Arnold camera

## [7.4.3.2] 2025-09-24

### Bug Fixes

- [usd#2422](https://github.com/Autodesk/arnold-usd/issues/2422) - Fix regression on frame changes in the usd procedural

## [7.4.3.1] - 2025-08-27

### Bug Fixes

- [usd#2349](https://github.com/Autodesk/arnold-usd/issues/2349) - Husk renders with the Arnold product type overwrites the same output path when rendering mutliple frames in same process
- [usd#2392](https://github.com/Autodesk/arnold-usd/issues/2392) - Fix a warning due to additional metadata parameters, typeName and colorSpace introduced in USD 25.05.

## [7.4.3.0] - 2025-07-25

### Features

- [usd#2307](https://github.com/Autodesk/arnold-usd/issues/2307) - DomeLight connections are taken into account twice when written from an arnold scene
- [usd#1405](https://github.com/Autodesk/arnold-usd/issues/1405) - Combine DomeLight texture, color and temperature properly in usd and hydra
- [usd#2310](https://github.com/Autodesk/arnold-usd/issues/2310) - Support volume shaders on points with hydra
- [usd#2320](https://github.com/Autodesk/arnold-usd/issues/2320) - Use overwrite mode for stats and deprecate the render setting stats:mode
- [usd#2337](https://github.com/Autodesk/arnold-usd/issues/2337) - Don't disable CER error reports through the hydra procedural
- [usd#2346](https://github.com/Autodesk/arnold-usd/issues/2346) - Registry should explicitely consider the usd.hide metadata instead of DCC-specific ones
- [usd#2352](https://github.com/Autodesk/arnold-usd/issues/2352) - Changing the frame should not re-initialize the procedural

### Bug Fixes

- [usd#2333](https://github.com/Autodesk/arnold-usd/issues/2333) - Fix crash in USD writer when the outputs string contains asterisk
- [usd#2364](https://github.com/Autodesk/arnold-usd/issues/2364) - Fix crash render delegate when using indexed uvs and shared arrays.

## [7.4.2.2] - 2025-07-03

### Bug Fixes

- [usd#2340](https://github.com/Autodesk/arnold-usd/issues/2340) - Fix warnings when nodes are deleted during batch sessions
- [usd#2334](https://github.com/Autodesk/arnold-usd/issues/2334) - Fix random crashes with husk and cryptomatte

## [7.4.2.1] - 2025-06-09

### Bug Fixes

- [usd#2303](https://github.com/Autodesk/arnold-usd/issues/2303) - Improve detection of hidden primitives that should be skipped
- [usd#2309](https://github.com/Autodesk/arnold-usd/issues/2309) - Fix recent conflict between primitives visibility and purpose
- [usd#2296](https://github.com/Autodesk/arnold-usd/issues/2296) - Proper support of stats mode in the render delegate
- [usd#2313](https://github.com/Autodesk/arnold-usd/issues/2313) - Arnold primvars aren't taken into account for ArnoldProceduralCustom primitives in usd

## [7.4.2.0] - 2025-05-16

### Features

- [usd#2264](https://github.com/Autodesk/arnold-usd/issues/2264) - Skip translation of invisible primitives in the render delegate
- [usd#2277](https://github.com/Autodesk/arnold-usd/issues/2277) - Ensure child nodes are properly destroyed in the hydra procedural
- [usd#2276](https://github.com/Autodesk/arnold-usd/issues/2276) - Improve default interactive FPS settings in the render delegate
- [usd#2284](https://github.com/Autodesk/arnold-usd/issues/2284) - Use a single display driver in the render delegate
- [usd#2231](https://github.com/Autodesk/arnold-usd/issues/2231) - Fix velocity motion blur coherence when there is varying number of instances
- [usd#2285](https://github.com/Autodesk/arnold-usd/issues/2285) - Use point instancer angular velocity in the render delegate
- [usd#2260](https://github.com/Autodesk/arnold-usd/issues/2260) - Deepexr settings were not set properly with husk renders
- [usd#2287](https://github.com/Autodesk/arnold-usd/issues/2287) - Fix mismatch in default value for GI_transmission_depth between USD and Hydra
- [usd#2205](https://github.com/Autodesk/arnold-usd/issues/2205) - The node registry uses sdr instead of ndr in versions higher or equal to 25.05

### Bug fixes

- [usd#2298](https://github.com/Autodesk/arnold-usd/issues/2298) - Fix a potential crash when multiple hydra readers initialize concurrently.
- [usd#2300](https://github.com/Autodesk/arnold-usd/issues/2300) - Animated sequences render an incorrect filename in hydra mode

## [7.4.1.1] - 2025-05-07

### Bug fixes

- [usd#2269](https://github.com/Autodesk/arnold-usd/issues/2269) - Connections are not processed correctly during hydra procedural updates
- [usd#2268](https://github.com/Autodesk/arnold-usd/issues/2268) - Reenable instancer motion blur on the first rendered frame by recomputing the transform matrices when the shutter is updated.

## [7.4.1.0] - 2025-03-26

### Features
- [usd#2148](https://github.com/Autodesk/arnold-usd/issues/2248) - Leverage new Shared Arrays API in the render delegate.
- [usd#2227](https://github.com/Autodesk/arnold-usd/issues/2227) - Hydra procedural breaks the Arnold logging settings
- [usd#2228](https://github.com/Autodesk/arnold-usd/issues/2228) - Release usd stage after the hydra procedural translation
- [usd#2240](https://github.com/Autodesk/arnold-usd/issues/2240) - Default volume shader should be assigned in the usd procedural
- [usd#2242](https://github.com/Autodesk/arnold-usd/issues/2242) - Support HDARNOLD_DEBUG_SCENE env var in the hydra procedural
- [usd#2248](https://github.com/Autodesk/arnold-usd/issues/2248) - Enable the hydra mode of the procedural by default

### Bug fixes
- [usd#2208](https://github.com/Autodesk/arnold-usd/issues/2208) - Fix unnecessary allocations in the instancer and mesh.
- [usd#2218](https://github.com/Autodesk/arnold-usd/issues/2218) - Fix Hydra warning with orthographic cameras
- [usd#2219](https://github.com/Autodesk/arnold-usd/issues/2219) - Fix race condition in hydra with node names
- [usd#2225](https://github.com/Autodesk/arnold-usd/issues/2225) - Fix crash in point instancers with missing prototypes
- [usd#2224](https://github.com/Autodesk/arnold-usd/issues/2224) - Fix warning "HdArnoldDriverMain is already installed" 
- [usd#2234](https://github.com/Autodesk/arnold-usd/issues/2234) - Fix warning "Selected hydra renderer doesn't support prim type 'RenderSettings'" 
- [usd#2232](https://github.com/Autodesk/arnold-usd/issues/2232) - Fix incorrect husk render of left handed indexed meshes with normals.
- [usd#2239](https://github.com/Autodesk/arnold-usd/issues/2239) - OpenVDB asset with explicit fieldName does not render in hydra
- [usd#1402](https://github.com/Autodesk/arnold-usd/issues/1402) - Incorrect transform when exporting parented objects to USD
- [usd#2254](https://github.com/Autodesk/arnold-usd/issues/2254) - Fix bug happening when the number of normal keys is less than the number of point keys.

## [7.4.0.0] - 2025-03-26

### Bug fixes
- [usd#2201](https://github.com/Autodesk/arnold-usd/issues/2201) - Hydra procedural should not modify the input usd stage with shutter range
- [usd#2197](https://github.com/Autodesk/arnold-usd/issues/2197) - MaterialX textures not applied properly in hydra through mayaHydra

## [7.3.7.0] - 2025-02-13

### Feature
- [usd#2160](https://github.com/Autodesk/arnold-usd/issues/2160) - Support OSL code generated from image shaders in MaterialX 1.38.10
- [usd#2170](https://github.com/Autodesk/arnold-usd/issues/2170) - Fix handling of render tags in hydra.
- [usd#2176](https://github.com/Autodesk/arnold-usd/issues/2176) - Support distant light's normalize attribute
- [usd#2177](https://github.com/Autodesk/arnold-usd/issues/2177) - Declare render delegate's stop render instead of pause.
- [usd#2183](https://github.com/Autodesk/arnold-usd/issues/2183) - Enable JUnit reports
- [usd#2190](https://github.com/Autodesk/arnold-usd/issues/2190) - Save the time taken to read the stage in the stats. 

### Bug fixes
- [usd#2159](https://github.com/Autodesk/arnold-usd/issues/2159) - User data errors with deformed meshes and subdivision
- [usd#2168](https://github.com/Autodesk/arnold-usd/issues/2168) - Workaround a bug in USD >= 24.08 by resampling values returned by SamplePrimvar.
- [usd#2187](https://github.com/Autodesk/arnold-usd/issues/2187) - Cannot override output image with arnold product types 

## [7.3.6.0] - 2024-12-12

### Feature
- [usd#2119](https://github.com/Autodesk/arnold-usd/issues/2119) - Support options shader_override attribute
- [usd#2088](https://github.com/Autodesk/arnold-usd/issues/2088) - Remove deprecated schemas
- [usd#2110](https://github.com/Autodesk/arnold-usd/issues/2110) - Represent arnold operators as shader primitives
- [usd#2145](https://github.com/Autodesk/arnold-usd/issues/2145) - First support of versioned schemas

### Bug fixes
- [usd#2129](https://github.com/Autodesk/arnold-usd/issues/2129) - Fixed crashes when instancers have empty / invalid positions
- [usd#2131](https://github.com/Autodesk/arnold-usd/issues/2131) - Wrong transform when an instanceable prim is not xformable
- [usd#2133](https://github.com/Autodesk/arnold-usd/issues/2133) - Fixed crash when the root primitive is invalid
- [usd#2122](https://github.com/Autodesk/arnold-usd/issues/2122) - RectLight doesn't take width / height into account with scenes exported from Arnold
- [usd#1764](https://github.com/Autodesk/arnold-usd/issues/1764) - ArnoldUsd schema was missing from Arnold SDK
- [usd#2154](https://github.com/Autodesk/arnold-usd/issues/2154) - Husk renders can miss scene updates

## [7.3.5.0] - 2024-11-08

### Feature
- [usd#1738](https://github.com/Autodesk/arnold-usd/issues/1738) - Support all camera Arnold attributes in Hydra
- [usd#1965](https://github.com/Autodesk/arnold-usd/issues/1965) - Write color manager attributes in the RenderSettings primitive
- [usd#1974](https://github.com/Autodesk/arnold-usd/issues/1974) - Delegate should only create default shaders when needed
- [usd#1959](https://github.com/Autodesk/arnold-usd/issues/1959) - Improve translation of normals and primvars in hydra
- [usd#1946](https://github.com/Autodesk/arnold-usd/issues/1946) - Support color space in materialx for hydra
- [usd#1972](https://github.com/Autodesk/arnold-usd/issues/1972) - Ensure subdivision is disabled when subdiv iterations is equal to 0 in Hydra
- [usd#1982](https://github.com/Autodesk/arnold-usd/issues/1982) - Fix subdivision when primvars are set in parent primitives
- [usd#1979](https://github.com/Autodesk/arnold-usd/issues/1979) - Support treatAsPoint in Hydra photometric lights
- [usd#1987](https://github.com/Autodesk/arnold-usd/issues/1987) - Author familyName and familyType in GeomSubsets written as USD
- [usd#2000](https://github.com/Autodesk/arnold-usd/issues/2000) - Write light filters through node graphs so they can be rendered in Hydra
- [usd#1997](https://github.com/Autodesk/arnold-usd/issues/1997) - Apply correct amount of transform keys when xformOp is set on parent prims
- [usd#2003](https://github.com/Autodesk/arnold-usd/issues/2003) - Choose render settings primitive through hydra scene loader
- [usd#2019](https://github.com/Autodesk/arnold-usd/issues/2019) - Support purpose in the hydra procedural
- [usd#2010](https://github.com/Autodesk/arnold-usd/issues/2010) - Support TreatAsPoint in spot lights
- [usd#2017](https://github.com/Autodesk/arnold-usd/issues/2017) - Set root transform and node id in the hydra procedural
- [usd#2015](https://github.com/Autodesk/arnold-usd/issues/2015) - Support hydra points with empty widths
- [usd#2008](https://github.com/Autodesk/arnold-usd/issues/2008) - Write spot and photometric lights as UsdLux schemas
- [usd#2030](https://github.com/Autodesk/arnold-usd/issues/2030) - Write background and atmosphere shaders under a node graph for hydra support
- [usd#2031](https://github.com/Autodesk/arnold-usd/issues/2031) - Write AOV shaders under a node graph for hydra support
- [usd#2025](https://github.com/Autodesk/arnold-usd/issues/2025) - Write imagers through node graphs for hydra support
- [usd#2042](https://github.com/Autodesk/arnold-usd/issues/2042) - Follow hydra normals skinning behavior in the procedural.
- [usd#1174](https://github.com/Autodesk/arnold-usd/issues/1174) - When the render errors or is aborted, husk will now exit with error code (houdini >= 20.5)
- [usd#2057](https://github.com/Autodesk/arnold-usd/issues/2057) - Add Ginstance support in hydra and fix a data race issue.
- [usd#2055](https://github.com/Autodesk/arnold-usd/issues/2055) - Support animated curves orientations in hydra
- [usd#2053](https://github.com/Autodesk/arnold-usd/issues/2053) - Visibility and sidedness attributes not supported in Arnold native hydra prims
- [usd#2058](https://github.com/Autodesk/arnold-usd/issues/2058) - Support UsdPlane primitives
- [usd#2061](https://github.com/Autodesk/arnold-usd/issues/2061) - Support arnold light groups in Hydra
- [usd#2064](https://github.com/Autodesk/arnold-usd/issues/2064) - Support instances of ArnoldProceduralCustom primitives
- [usd#2067](https://github.com/Autodesk/arnold-usd/issues/2067) - Do not author useless "normals" user data in meshes/curves through the procedural
- [usd#1118](https://github.com/Autodesk/arnold-usd/issues/1118) - Add profile/stats settings to the Render Settings
- [usd#2080](https://github.com/Autodesk/arnold-usd/issues/2080) - Author animated shader attributes in a way that they can render in hydra
- [usd#2082](https://github.com/Autodesk/arnold-usd/issues/2082) - Support arnold cameras in hydra
- [usd#2084](https://github.com/Autodesk/arnold-usd/issues/2084) - Imagers should be applied to all drivers
- [usd#2086](https://github.com/Autodesk/arnold-usd/issues/2086) - Compute FOV in the procedural and hydra in a similar manner
- [usd#2047](https://github.com/Autodesk/arnold-usd/issues/2047) - Shaders exports should be bound to a material
- [usd#2109](https://github.com/Autodesk/arnold-usd/issues/2109) - Expose hydra parameter in the procedural
- [usd#2107](https://github.com/Autodesk/arnold-usd/issues/2107) - Support procedural updates in hydra mode
- [usd#2111](https://github.com/Autodesk/arnold-usd/issues/2111) - Add support for transform_keys in xform primitives

### Bug fixes
- [usd#1961](https://github.com/Autodesk/arnold-usd/issues/1961) - Random curves width in Hydra when radius primvars are authored
- [usd#1977](https://github.com/Autodesk/arnold-usd/issues/1977) - Aov shaders not set properly in hydra mode of the scene format
- [usd#1984](https://github.com/Autodesk/arnold-usd/issues/1984) - Cylinder lights not taking normalization into account through USD
- [usd#1994](https://github.com/Autodesk/arnold-usd/issues/1994) - Fixed hydra errors with varying topologies, and incorrect velocities in first renders.
- [usd#1992](https://github.com/Autodesk/arnold-usd/issues/1992) - Support hydra skinned positions with more than 3 keys
- [usd#2027](https://github.com/Autodesk/arnold-usd/issues/2027) - Fix faceVarying normals interpolation in the procedural when the mesh is left handed.
- [usd#1837](https://github.com/Autodesk/arnold-usd/issues/1837) - Fix motion blur of instanced skinned geometry with animated parent matrix
- [usd#2037](https://github.com/Autodesk/arnold-usd/issues/2027) - Improve instances and objects motion blur coherence.
- [usd#2078](https://github.com/Autodesk/arnold-usd/issues/2078) - Ensure the hydra render callback is always invoked
- [usd#2094](https://github.com/Autodesk/arnold-usd/issues/2094) - Support material interactive updates in the procedural
- [usd#2092](https://github.com/Autodesk/arnold-usd/issues/2092) - Fix interactive update issue when prims visibility is tweaked in the procedural
- [usd#2102](https://github.com/Autodesk/arnold-usd/issues/2102) - Remove hydra warning subdiv_iterations: use type BYTE, not INT 
- [usd#2105](https://github.com/Autodesk/arnold-usd/issues/2105) - Ensure the Arnold scene isn't modified after a Hydra batch render started
- [usd#2127](https://github.com/Autodesk/arnold-usd/issues/2127) - Support deform_keys in curves

### Build
- [usd#1969](https://github.com/Autodesk/arnold-usd/issues/1969) - Remove support for USD versions older than 21.05

## [7.3.4.1] - 2024-09-18
- [usd#2090](https://github.com/Autodesk/arnold-usd/issues/2090) - Fixed crashes when registering the TfNotice callback multiple times

## [7.3.4.0] - 2024-08-30
- [usd#2075](https://github.com/Autodesk/arnold-usd/issues/2075) - Ensure options attributes are not set while a hydra render is in progress

## [7.3.3.1] - 2024-08-09
- [usd#1989](https://github.com/Autodesk/arnold-usd/issues/1989) - Support mixed half/float channels when using the render delegate in batch mode with husk.
- [usd#1610](https://github.com/Autodesk/arnold-usd/issues/1610) - Proper support of arnold:visibility primvar in hydra

## [7.3.3.0] - 2024-07-25

### Feature
- [usd#1814](https://github.com/Autodesk/arnold-usd/issues/1814) - Support skinning on USD curves and points
- [usd#1939](https://github.com/Autodesk/arnold-usd/issues/1939) - Support primvars as user data on lights
- [usd#1950](https://github.com/Autodesk/arnold-usd/issues/1950) - Avoid creating a render delegate in batch mode when ARNOLD_FORCE_ARBORT_ON_LICENSE_FAIL is set and the license isn't found.
- [usd#1918](https://github.com/Autodesk/arnold-usd/issues/1918) - Use batch render sessions for husk renders
- [usd#1955](https://github.com/Autodesk/arnold-usd/issues/1955) - Improve USD authoring of quad and mesh lights in the writer

### Bug fixes
- [usd#1861](https://github.com/Autodesk/arnold-usd/issues/1861) - Fix BasisCurves disappearing on interactive updates
- [usd#1927](https://github.com/Autodesk/arnold-usd/issues/1927) - Fix procedural updates during iteractive changes of non-leaf primitives
- [usd#1661](https://github.com/Autodesk/arnold-usd/issues/1661) - In the procedural the subdivision meshes will use the normals generated by the subdivision algorithm instead of the normal primvar.
- [usd#1919](https://github.com/Autodesk/arnold-usd/issues/1919) - Fix rendering multiple frames with husk.
- [usd#1952](https://github.com/Autodesk/arnold-usd/issues/1952) - Don't write camera aperture parameters if they're already set
- [usd#1902](https://github.com/Autodesk/arnold-usd/issues/1902) - Fix invalid Cache ID sporadic error
- [usd#1940](https://github.com/Autodesk/arnold-usd/issues/1940) - Incorrect handling of shaders referenced in multiple materials

## [7.3.2.1] - 2024-06-19

### Bug fixes
- [usd#1923](https://github.com/Autodesk/arnold-usd/issues/1923) - Fix instance primvar indices with multiple prototypes
- [usd#1929](https://github.com/Autodesk/arnold-usd/issues/1929) - Ensure subdiv_iterations is not set uselessly during procedural updates
- [usd#1932](https://github.com/Autodesk/arnold-usd/issues/1932) - Fix a crash when the number of elements in a primvar should be equal to the number of points but is not.

## [7.3.2.0] - 2024-05-22

### Feature
- [usd#1894](https://github.com/Autodesk/arnold-usd/issues/1894) - Write cylinder lights as UsdLuxCylinderLight primitives

### Bug fixes
- [usd#1900](https://github.com/Autodesk/arnold-usd/issues/1900) - Fix transform hierarchies for Arnold non-xformable primitives
- [usd#1908](https://github.com/Autodesk/arnold-usd/issues/1908) - Read deform_keys independently of the primvar interpolation
- [usd#1903](https://github.com/Autodesk/arnold-usd/issues/1903) - USD Writer should skip materials when the shader mask is disabled
- [usd#1906](https://github.com/Autodesk/arnold-usd/issues/1906) - Fix light filters assignment order in the render delegate to make it consistent with the procedural.
- [usd#1912](https://github.com/Autodesk/arnold-usd/issues/1912) - Procedural interactive updates don't consider primitives visibility
- [usd#1914](https://github.com/Autodesk/arnold-usd/issues/1914) - Ensure Mesh vertex attributes are not written to USD when not set in Arnold 

## [7.3.1.0] - 2024-03-27

### Feature
- [usd#1730](https://github.com/Autodesk/arnold-usd/issues/1730) - Add light linking to the ArnoldProceduralCustom when using hydra.
- [usd#168](https://github.com/Autodesk/arnold-usd/issues/168) - Support interactive USD updates in the procedural
- [usd#1835](https://github.com/Autodesk/arnold-usd/issues/1835) - Support Arnold "help" metadata instead of previous "desc" metadata.
- [usd#1865](https://github.com/Autodesk/arnold-usd/issues/1865) - Support ArnoldOptions primitives in Hydra
- [usd#1852](https://github.com/Autodesk/arnold-usd/issues/1852) - Write Arnold options as UsdRenderSettings primitives
- [usd#1870](https://github.com/Autodesk/arnold-usd/issues/1870) - Use new node type AI_NODE_IMAGER
- [usd#1878](https://github.com/Autodesk/arnold-usd/issues/1878) - Make arnold relative path optional for image shaders
- [usd#1874](https://github.com/Autodesk/arnold-usd/issues/1874) - Shader output attributes should be outputs:out to match the Sdr registry
- [usd#1873](https://github.com/Autodesk/arnold-usd/issues/1873) - Ensure materials are written under a scope primitive
- [usd#1868](https://github.com/Autodesk/arnold-usd/issues/1868) - Support velocities in PointInstancer primitive rendered through the procedural
- [usd#1889](https://github.com/Autodesk/arnold-usd/issues/1889) - GI Transmission Depth should default to 8 in the Render Settings

### Bug fixes
- [usd#1547](https://github.com/Autodesk/arnold-usd/issues/1547) - Fix mesh lights shutoff when there is a light link in the scene.
- [usd#1859](https://github.com/Autodesk/arnold-usd/issues/1859) - Support PointInstancer invisibleIDs for lights 
- [usd#1881](https://github.com/Autodesk/arnold-usd/issues/1881) - Support UDIM and relative paths on mtlx image shaders
- [usd#1884](https://github.com/Autodesk/arnold-usd/issues/1884) - Set a proper name to skydome image node in Hydra
- [usd#1890](https://github.com/Autodesk/arnold-usd/issues/1890) - Reduce VtArray memory consumption, mostly in the instancer. 

## [7.3.0.0] - 2024-03-27

### Feature
- [usd#1758](https://github.com/Autodesk/arnold-usd/issues/1758) - Return a default value when an attribute type is not recognized
- [usd#1759](https://github.com/Autodesk/arnold-usd/issues/1759) - Remove GeometryLight usd imaging adapter
- [usd#1705](https://github.com/Autodesk/arnold-usd/issues/1705) - Support Point instancers having lights as prototypes
- [usd#1635](https://github.com/Autodesk/arnold-usd/issues/1635) - Support arnold visibility and matte on Hydra instances
- [usd#1806](https://github.com/Autodesk/arnold-usd/issues/1806) - Extend the WriteUsdStageCache API

### Bug fixes
- [usd#1756](https://github.com/Autodesk/arnold-usd/issues/1756) - Registry should declare filenames as assets in GetTypeAsSdfType 
- [usd#1770](https://github.com/Autodesk/arnold-usd/issues/1770) - Fix exr driver always rendering float with husk when productType is arnold
- [usd#1772](https://github.com/Autodesk/arnold-usd/issues/1772) - RectLight texture uvs are now consistent between husk, kick and other renderers.
- [usd#1776](https://github.com/Autodesk/arnold-usd/issues/1776) - Fix incorrect PointInstancer instance orientations in the render delegate.
- [usd#1769](https://github.com/Autodesk/arnold-usd/issues/1769) - Fix curve uvs when they are vertex interpolated.
- [usd#1784](https://github.com/Autodesk/arnold-usd/issues/1784) - The aov layer name is now correctly taken into account when rendering exrs with husk and using the arnold productType.

### Build
- [usd#1795](https://github.com/Autodesk/arnold-usd/issues/1795) - Fix compilation issue on macOS with clang 15.0.7.
- [usd#1793](https://github.com/Autodesk/arnold-usd/issues/1793) - Enable compiling arnold-usd without USD_BIN.

## [fix-7.2.5]

### Bug fixes
- [usd#1854](https://github.com/Autodesk/arnold-usd/issues/1854) - Fix unnecessary updating of the scene in the render delegate when the camera is moving. This improves the interactivity in Solaris.

## [7.2.5.2] and [7.2.5.3]

### Bug fixes
- [usd#1808](https://github.com/Autodesk/arnold-usd/issues/1808) - Fix the error "Cannot load _htoa_pygeo library required for volume rendering in Solaris" in Houdini 19.5+.
- [usd#1812](https://github.com/Autodesk/arnold-usd/issues/1812) - Improve Material network creation by caching the node entries and the osl code.
- [usd#1781](https://github.com/Autodesk/arnold-usd/issues/1781) - Fix a crash happening in a aiStandin usd when scrolling the timeline in maya.
- [usd#1753](https://github.com/Autodesk/arnold-usd/issues/1753) - Fix a problem with yeti where the transforms of the ArnolProceduralCustom were not taken into account in kick (procedural).

## [7.2.5.1] - 2024-01-18

### Bug fixes

- [usd#1776](https://github.com/Autodesk/arnold-usd/issues/1776) - Fix incorrect PointInstancer instance orientations in the render delegate.
- [usd#1769](https://github.com/Autodesk/arnold-usd/issues/1769) - Fix curve uvs when they are vertex interpolated.

## [7.2.5.0] - 2023-12-13

### Feature
- [usd#612](https://github.com/Autodesk/arnold-usd/issues/612) - Add support for orthographic camera in the hydra render delegate.
- [usd#1726](https://github.com/Autodesk/arnold-usd/issues/1726) - Add usdz as a supported format of the scene reader
- [usd#1077](https://github.com/Autodesk/arnold-usd/issues/1077) - Support --threads / -j argument in husk to control the amount of render threads
- [usd#1748](https://github.com/Autodesk/arnold-usd/issues/1748) - Support implicit conversions between Float3 and Float4
- [usd#658](https://github.com/Autodesk/arnold-usd/issues/658) - Support pixel aspect ratio in Hydra
- [usd#1746](https://github.com/Autodesk/arnold-usd/issues/1746) - Made the behaviour for doubleSided gprims consistent between USD and Hydra

### Bug fixes
- [usd#1709](https://github.com/Autodesk/arnold-usd/issues/1709) - Procedural failures if schemas are present
- [usd#1713](https://github.com/Autodesk/arnold-usd/issues/1713) - Fix coding error "attempt to get string for GfVec4f"
- [usd#1732](https://github.com/Autodesk/arnold-usd/issues/1732) - Force the "color" AOV to be interpreted as the Arnold beauty pass.
- [usd#1735](https://github.com/Autodesk/arnold-usd/issues/1735) - Fix usdskel geometry and motion blur interpolation outside the keyframe boundaries.
- [usd#1524](https://github.com/Autodesk/arnold-usd/issues/1524) - Fix material binding on instances under a SkelRoot
- [usd#1718](https://github.com/Autodesk/arnold-usd/issues/1718) - Support primvars:arnold attributes in Arnold typed schemas

## [7.2.4.1] - 2023-10-18

### Bugfix
- [usd#1678](https://github.com/Autodesk/arnold-usd/issues/1678) - Add support for Arnold shaders with multiple outputs
- [usd#1711](https://github.com/Autodesk/arnold-usd/issues/1711) - Fix duplicated arnold user data introduced in 7.2.3.0
- [usd#1728](https://github.com/Autodesk/arnold-usd/issues/1728) - Fix Cryptomatte compatibility with Nuke.
- [usd#1757](https://github.com/Autodesk/arnold-usd/issues/1757) - Declare closure attributes as terminals in the registry

## [7.2.4.0] - 2023-10-04

### Feature
- [usd#1634](https://github.com/Autodesk/arnold-usd/issues/1615) - Support curves orientations identically between USD and Hydra / Prevent errors due to bad curves orientations count.
- [usd#1615](https://github.com/Autodesk/arnold-usd/issues/1615) - add bespoke usdgenschema command to create arnold schema without python
- [usd#739](https://github.com/Autodesk/arnold-usd/issues/739) - Implement the ArnoldProceduralCustom prim in hydra.
- [usd#1644](https://github.com/Autodesk/arnold-usd/issues/1644) - Support nodes mask in the hydra procedural
- [usd#1656](https://github.com/Autodesk/arnold-usd/issues/1656) - Use the same tessellation for sphere primitives as Hydra
- [usd#1632](https://github.com/Autodesk/arnold-usd/issues/1632) - Support custom materialx node definitions placed in a folder defined by the environment variable PXR_MTLX_STDLIB_SEARCH_PATHS
- [usd#1603](https://github.com/Autodesk/arnold-usd/issues/1603) - Support custom USD materialx shaders with any shader type
- [usd#1586](https://github.com/Autodesk/arnold-usd/issues/1586) - Support motion blur in the hydra procedural
- [usd#1587](https://github.com/Autodesk/arnold-usd/issues/1587) - Support object path in the hydra procedural
- [usd#1664](https://github.com/Autodesk/arnold-usd/issues/1664) - Support hydra procedural with a cacheID

### Bug fixes
- [usd#1613](https://github.com/Autodesk/arnold-usd/issues/1613) - Invisible Hydra primitives should ignore arnold visibility
- [usd#1641](https://github.com/Autodesk/arnold-usd/issues/1641) - Ensure nodes created by the render delegate have the correct parent procedural.
- [usd#1652](https://github.com/Autodesk/arnold-usd/issues/1652) - Restore the schema installed files organisation. 
- [usd#1673](https://github.com/Autodesk/arnold-usd/issues/1673) - UsdUvTexture ignores missing textures in hydra
- [usd#1675](https://github.com/Autodesk/arnold-usd/issues/1675) - Fix UsdUvTexture default wrap modes and uvset coordinates.
- [usd#1657](https://github.com/Autodesk/arnold-usd/issues/1657) - Fix a motion blur sampling bug happening when a mesh has facevarying indexed normals and different number of indices per key frame.
- [usd#1693](https://github.com/Autodesk/arnold-usd/issues/1693) - Fix geometry light not rendering in recent version.
- [usd#1696](https://github.com/Autodesk/arnold-usd/issues/1696) - Fix cryptomatte render by restoring previous filter assignment to the default filter.

### Build
- [usd#1648](https://github.com/Autodesk/arnold-usd/issues/1648) - Fix schemas generation issue that was intermittently failing

## [7.2.3.2] - 2023-08-30

### Bug fixes
- [usd#1605](https://github.com/Autodesk/arnold-usd/issues/1605) - Apply the MaterialBindingAPI to the bound prims when converting ass to usd
- [usd#1607](https://github.com/Autodesk/arnold-usd/issues/1607) - Allow primvars with namespace in the procedural 
- [usd#1593](https://github.com/Autodesk/arnold-usd/issues/1593) - Fix crash in the procedural when the UsdPrimvarReader varname attribute is not set.
- [usd#1625](https://github.com/Autodesk/arnold-usd/issues/1625) - Fix issue where user defined primvars were reset.

## [7.2.3.1] - 2023-08-14

### Note
- 7.2.3.1 is an Arnold hot fix, no new changes in arnold-usd. See 7.2.3.0 for the most recent changes.

## [7.2.3.0] - 2023-08-14

### Feature
- [usd#1435](https://github.com/Autodesk/arnold-usd/issues/1435) - Support "vertex" UV coordinates on Curves in the render delegate
- [usd#1579](https://github.com/Autodesk/arnold-usd/issues/1579) - Curves without any width should render with a default value

### Bug fixes
- [usd#1538](https://github.com/Autodesk/arnold-usd/issues/1538) - Fix triplanar in USD Materialx
- [usd#1588](https://github.com/Autodesk/arnold-usd/issues/1588) - Arnold schemas under a point instancer should be hidden
- [usd#1597](https://github.com/Autodesk/arnold-usd/issues/1597) - Fix hdx dependency which was causing issues on linux with husk
- [usd#1595](https://github.com/Autodesk/arnold-usd/issues/1595) - Support Arnold RenderVar filters in Hydra 

## [7.2.2.1] - 2023-06-21

### Bug fixes
- [usd#1567](https://github.com/Autodesk/arnold-usd/issues/1567) - Fix metallic attribute in UsdPreviewSurface in the render delegate
- [usd#1550](https://github.com/Autodesk/arnold-usd/issues/1550) - UsdPrimvarReader_float2 returning "st" not working in the usd procedural
- [usd#1552](https://github.com/Autodesk/arnold-usd/issues/1552) - Retain attributes ordering in the Sdr registry
- [usd#1548](https://github.com/Autodesk/arnold-usd/issues/1548) - Fix RenderProduct arnold:driver ignored in the render delegate
- [usd#1546](https://github.com/Autodesk/arnold-usd/issues/1546) - Fix relative paths on arnold nodes


## [7.2.2.0] - 2023-06-07

### Feature
- [usd#1492](https://github.com/Autodesk/arnold-usd/issues/1492) - Add Arnold render status and estimated render time to viewport annotation
- [usd#1499](https://github.com/Autodesk/arnold-usd/issues/1499) - Add support for camera filtermap and uv_remap
- [usd#1486](https://github.com/Autodesk/arnold-usd/issues/1486) - Add a "Mtl" scope for materials when authoring usd files
- [usd#1529](https://github.com/Autodesk/arnold-usd/issues/1529) - Add AI_RAY_SUBSURFACE visibility flag support in the render delegate and procedural

### Build
- [usd#1480](https://github.com/Autodesk/arnold-usd/issues/1480) - Allow to specify a testsuite output folder
- [usd#1463](https://github.com/Autodesk/arnold-usd/issues/1463) - Fix Windows builds with CMake
- [usd#1471](https://github.com/Autodesk/arnold-usd/issues/1471) - Support relative paths for Arnold and USD Sdk
- [usd#1466](https://github.com/Autodesk/arnold-usd/issues/1466) - Allow to run the testsuite without any build of the procedural
- [usd#1508](https://github.com/Autodesk/arnold-usd/issues/1508) - Support relative paths for python / boost / tbb
- [usd#1512](https://github.com/Autodesk/arnold-usd/issues/1512) - Remove deprecated ENABLE_MATERIALX build variable
- [usd#1519](https://github.com/Autodesk/arnold-usd/issues/1519) - Procedural does not compile with older versions of Arnold

### Bug fixes
- [usd#1502](https://github.com/Autodesk/arnold-usd/issues/1502) - Render delegate crashes with empty arrays.
- [usd#1522](https://github.com/Autodesk/arnold-usd/issues/1522) - Support UsdPrimvarReader_float2 shader returning the "st" variable
- [usd#1530](https://github.com/Autodesk/arnold-usd/issues/1530) - Fix a crash when a user primvars has an empty array on a keyframe
- [usd#1535](https://github.com/Autodesk/arnold-usd/issues/1535) - Fixed Render delegate crashes when visibility is set on lights
- [usd#1532](https://github.com/Autodesk/arnold-usd/issues/1532) - Schemas are not declaring asset parameters for filenames
- [usd#1525](https://github.com/Autodesk/arnold-usd/issues/1525) - Default values for AA sampling and ray depths in direct USD renders are now increased to be equal to render delegate defaults.

## [7.2.1.1] - 2023-04-19 

### Bug fixes
- [usd#1426](https://github.com/Autodesk/arnold-usd/issues/1426) - Skinned transforms are now correctly used on the skinned meshes.
- [usd#1485](https://github.com/Autodesk/arnold-usd/issues/1485) - MaterialX shader nodes should have "auto" colorspace by default 
- [usd#1477](https://github.com/Autodesk/arnold-usd/issues/1477) - A a note in the README for the flickering issue with instances which can be fixed with the `USD_ASSIGN_PROTOTYPES_DETERMINISTICALLY` environment variable. 
- [usd#1462](https://github.com/Autodesk/arnold-usd/issues/1462) - Ensure shader scope doesn't appear twice in the hierarchy.
- [usd#1459](https://github.com/Autodesk/arnold-usd/issues/1459) - Support Shaders with multiple outputs
- [usd#1359](https://github.com/Autodesk/arnold-usd/issues/1359) - Refresh the arnold instancer when the prototype mesh points have changed.
- [usd#1483](https://github.com/Autodesk/arnold-usd/issues/1483) - Indexed normals with vertex interpolation are now converted properly.

## [7.0.0.1] - 2021-11-24

### Bugfixes

#### Build
- #923 Testsuite fails with Arnold 7
- #1487 Update Scons to build on windows with MSVC 14.3 

#### Procedural
- #458 Point instancer should prune the primitives under its hierarchy
- #904 No way to have Point Instancer prototypes hidden
- #921 Remap curves primvars to avoid "wrong data count" errors
- #928 Transform from the Point Instancer is not applied to instances

#### Imaging
- #902 Render session is not passed to AiDeviceAutoSelect
- #900 Avoid calling AiRenderBegin if render is already running
- #905 Integer primvar Render Vars fail to render via husk
- #906 Version is not set as default when returning SDR definitions
- #915 Volume shader is not applied to the ArnoldVolume primitive in Hydra
- #918 Rendervars do not show up in Houdini 19

#### Scene Format
- #924 Writer now saves the default color manager node

## [7.0.0.0] - 2021-10-18

### Enhancements

#### Build
- **ARNOLD_ prefix for definitions**: Arnold-USD now uses ARNOLD_ prefix for definitions to differentiate from core definitions. (#823)
- **CMake testsuite**: The cmake build system is now capable of running tests via kick and usdrecord. (#124 #27)
- **BOOST_ALL_NO_LIB**: When building via CMake, the implicit linking of boost libraries can be disabled.

#### Procedural
- **Procedural Path Mapping**: The procedural now supports Arnold Path Mapping when loading USD files. (#818)
- **Light Linking**: The procedural now supports light linking. (#787)

#### Imaging
- **UsdImaging adapters**: Arnold-USD now includes a set of UsdImaging adapters for Arnold specific schemas, that allows direct use of procedurals and Arnold shapes in Hydra. (#185 #741)
- **Deep rendering**: The render delegate now supports rendering of deep AOVs via [DelegateRenderProducts](https://www.sidefx.com/docs/hdk/_h_d_k__u_s_d_hydra.html#HDK_USDHydraHusk). (#650)
- **Hydra scene delegate**: Arnold-USD now includes an experimental scene delegate for Hydra. (#764)
- **Progressive disabled when using Husk**: Progressive rendering is now disabled when rendering via husk. (#755)
- **Custom PrimID Hydra Buffer**: The render delegate now uses a dedicated primId AOV to support selections in Hydra viewports. This improves support for render-time procedurals and leaves the built-in id parameter unchanged on shapes. (#812)
- **DoubleSided in the Render Delegate**: The render delegate now supports the built-in doubleSided parameter on USD primitives and correctly supports overrides via Arnold-specific primvars. (#805)
- **Motion Blur using Velocity and Acceleration**: The render delegate now extrapolates point positions if velocity or acceleration primvars exist and there are no multiple samples for the position. (#673)
- **Fast camera updates**: The render delegate now handles camera-only updates more efficiently, improving the first time to pixel. (#869)
- **Standard Surface Fallback**: The render delegate now uses a standard surface as a fallback, when no materials are assigned to a prim. (#861)
- **String array parameters**: String array primvars are now converted to built-in parameters. (#808)
- **Multiple hydra sessions**: The render delegate now uses the multiple render session API. (#783)

#### Scene Format
- **Multiple frames in a single file**: The USD writer is now able to append multiple frames to a single USD file. (#777)

### Bugfixes

#### Build
- #746 Issue with dependency between the procedural build and the testsuite
- #810 Can't build schemas when there are spaces in the project folder path
- #830 HdArnoldNativeRprim::GetBuiltinPrimvarNames lacks the override modifier
- #835 Building for USD 21.08 fails because the lack of SdfTypeIndicator
- #837 Schemas fail to generate with python 3 because of dict.iteritems()
- #845 Driver using AiOutputIteratorGetNext fails to compile when using a newer Arnold build
- #851 Don't configure plugInfo.json in-source
- #849 AiArrayGetXXXFuncs are not available anymore in newer Arnold builds
- #856 Cleanup solution introduced in #845
- #874 Allow to prepend PATH folders when building schemas
- #772 Testsuite fails when using USD 21.05/21.02
- #765 Can't compile when using USD 21.05
- #775 Remove deprecated functions, warnings, and fix some bugs
- #767 Unable to compile using cmake with a Python 3 build of USD
- #792 Build error using Houdini 18.0 on OSX

#### Procedural
- #847 Procedural should check the camera of the proper universe for motion blur settings
- #802 B-spline curves not using radius in procedural
- #816 The procedural does not use "driver:parameters:aov:name"

#### Imaging
- #751 Render delegate crashes when changing material terminals interactively
- #797 Warning messages when HdArnoldRenderPass is deleted
- #858 Can't hide/unhide lights in Hydra
- #853 Missing indices for facevarying primvars in Hydra abort renders
- #821 The render delegate crashes when using render session API
- #884 Render delegate shouldn't call AiBegin/AiEnd if Arnold is already active
- #843 Disabling render purposes does not hide geometries in hydra
- #887 Int parameters are not converted to unsigned int shader parameters in the render delegate
- #761 Change render_context string to RENDER_CONTEXT for render hints

#### Scene Format
- #871 Enforce writing multiple frames when no default is authored

#### Schemas
- #798 SdfMetadata Clashing with another NdrDiscovery Plugin

## [6.2.1.1] - 2021-06-07

### Bugfixes

#### Procedural
- #778 Procedural doesn't read some animated parameters properly
- #768 Custom typed primitives are not written if they already exist

#### Render Delegate
- #795 GfMatrix4d attributes are not converted to AI_TYPE_MATRIX parameters
- #790 Reset the disp_map parameter instead of setting nullptr when there is no displacement

## [6.2.1.0] - 2021-04-22

### Enhancements

#### Build
- **Common Library**: Arnold-usd now includes a set of common functions and string definitions to share across multiple modules. (#466)

#### Procedural
- **Half and Double precision**: Storing data using half or double precision is now supported. (#672)
- **Velocity blur**: The procedural now uses the velocity attribute to create motion keys for point-based shapes, when there are no position keys or the topology changes between frames. (#221)
- **NodeGraph schemas**: The procedural now supports using the NodeGraph schema for shader networks. (#678)
- **Crease Sets**: The procedural now supports crease sets on polymesh. (#694)
- **Purpose**: Usd Purpose is now supported in the procedural. (#698)
- **Transform2D**: The procedural now supports remapping UsdTransform2D to built-in Arnold nodes. (#517)
- **Multi-Threading**: The procedural now uses USD's WorkDispatcher which improves the performance of multi-threaded expansion in many cases. (#690)

#### Render Delegate
- **Half and Double precision**: Storing data using half or double precision is now supported. (#669)
- **Light and Shadow linking**: The render delegate now supports light and shadow linking. (#412)
- **Motion blur for the Point Instancer**: The render delegate now calculates motion blur when using the point instancer. (#653)
- **Pause and Resume**: Pausing and resuming renders are now supported in the render delegate. (#595)

#### Scene Format
- **Write with default values**: The scene format now supports optionally writing parameters with default values. (#720)

#### Schemas
- **Removal of the ArnoldUSD DSO**: The Schemas now work without generating a C++ library. This simplifies the build process and removes the need of installing DSOs that are not used. (#705)

### Bugfixes
- #715 Initialization order issue with constant strings common source file

#### Build
- #656 Arnold-USD fails to build with USD 21.02
- #663 render_delegate/lights.cpp fails to compile for pre-21.02 on windows
- #692 The render delegate fails to build on Windows due to template parameter names
- #707 Schema generator scons should use USD_BIN instead of USD_LOCATION + bin to find usdGenSchema
- #722 Failing to generate schemas when targeting Houdini on macOS
- #730 Translator fails to build when targeting USD 20.08

#### Procedural
- #674 Testsuite fails after standard_surface default changes in 6.2.0.0
- #564 Changing topology only works when rendering the first frame of the USD file
- #681 Read render settings at the proper frame
- #683 Don't apply skinning if the usdStage comes from the cache
- #508 Nested procedurals ignore matrix in the viewport API
- #687 Crash with empty primvar arrays
- #679 Attribute subdiv_type should have priority over usd subdivisionScheme
- #282 Primvars are not inherited from ancestor primitives
- #215 Issue with instanced primitives' visibility
- #244 Curves with vertex interpolation on width
- #732 Support wrap, bias and scale in USdUvTexture
- #724 ID not passed to the shapes generated in the procedural

#### Render Delegate
- #651 Error rendering USD file with samples in productName
- #660 Crease sets and subdivision scheme is not imported correctly
- #727 Arnold does not use wrapS and wrapT values on UsdUVTexture shader node when rendering UsdPreviewSurface
- #759 Primvars are not correctly set on the instancer if there is more than one prototype

#### Scene Format
- #615 USD Writer crashes when node name contains hyphen character
- #718 Inactive render vars are still rendered when using the scene format

## [6.2.0.1] - 2021-02-11

### Bugfixes

#### Render Delegate
- #654 Transform is not synced for the points primitive

## [6.2.0.0] - 2021-01-28

### Enhancements

#### Build

- **USD Procedural Name**: It is now possible to overwrite the USD procedurals name, which allows deploying a custom USD procedural alongside the core shipped one. (#600)

#### Procedural

- **Cache Id**: The procedural now supports reading stages from the shared stage cache via the cache id parameter. (#599)
- **Per ray-visibility**: The USD procedural now supports per-ray visibilities exported from Houdini. (#637)

#### Render Delegate

- **Hydra Cameras**: The render delegate now supports physical camera parameters, including depth of field and Arnold specific camera parameters. (#31 #591 #611)
- **Search Paths**: The render delegate now exposes search paths for plugins, procedurals, textures, and OSL includes. (#602)
- **Autobump Visibility**: The render delegate now supports setting autobump_visibility via primvars. (#597)

#### Scene Format

- **Authoring extent**: Extents on UsdGeom shapes are now correctly authored when using the USD scene format. (#582)

#### Schemas

- **Prefix for Schema Attributes**: Arnold schemas now prefix their attributes for better compatibility with built-in USD schemas. (#583)
- **Inheriting from UsdGeomXformable**: Arnold schemas now inherit from UsdGeomXformable instead of UsdTyped. (#558)
- **Creating XForms**: The USD scene format now correctly creates UsdGeomXform parents for shapes instead of UsdTyped. (#629)

### Bugfixes

#### Build

- #624 CMake fails building schemas for a beta Arnold build.
- #641 The render delegate fails to build using gcc 4.8.5 .

#### Procedural

- #621 UVs not read from facevarying primvar if indexes are not present.
- #643 Don't error out when a procedural object_path points at an inactive primitive.

#### Render Delegate

- #592 Invalid face-varying primvars crash the render delegate.
- #481 std::string, TfToken, and SdfAssetPath typed VtArrays are not converted when setting primvars.
- #619 Several built-in render buffer types are not translated to the right Arnold AOV type.
- #634 Fixing disappearing meshes when playing back animation.
- #638 Motion start and motion end is not set reading animated transformation.
- #605 Issues with UVs when rendering the kitchen scene.

#### Scene Format

- #596 Invalid USD is produced if polymesh is made of triangles and nsides is empty.

## [6.1.0.0] - 2020-10-28

### Enhancements

#### Build

- **Installing license**: The license is now copied to the installation folder when using scons. (#540)

#### Procedural

- **Light Shaping**: The procedural now supports the UsdLuxShapingAPI, allowing the use of spot and IES lights. (#344)

#### Render Delegate

- **UsdTransform2d**: The render delegate now supports the `UsdTransform2d` preview shader. (#516)
- **Per face material assignments**: Per-face material assignments are now supported. (#29)
- **Render Stats**: The Render Delegate now returns render stats via `GetRenderStats`. For now, this is used to show render progress in Solaris. (#537)

#### Schemas

- **Schema for custom procedurals**: The schemas now include ArnoldCustomProcedural for describing custom procedurals. (#487)
- **Schema updates**: Schemas now support cameras, render settings, and new output types. (#500)

#### Scene Format

- **Parent Scope**: There is a new flag to specify a custom root for all exported prims. (#292)
- **ST for Texture Coordinates**: Texture coordinates are now written as `primvars:st` to match the USD convention. (#542)

### Bugfixes

#### Build

- #528 Render delegate fails to build when building for Katana.
- #553 Compilation failures because of License.md.
- #567 HdArnoldMaterial fails to compile for the latest USD dev branch.
- #560 plugInfo.json for schemas is broken with newer USD builds.

#### Procedural

- #513 Viewport representation in points mode doesn't have the correct matrix.
- #543 Visibility is not checked on the current frame.
- #556 motion_start and motion_end is only set if the matrix of the prim is animated.
- #565 Animated transforms not translated properly if set on parent xforms.
- #570 USD procedural is not picking up osl shader parameters.

#### Render Delegate

- #569 Render delegate not picking up OSL shader parameters.
- #577 Point light should have zero radius.
- #580 The Render Delegate's depth range is incorrect if USD is at least version 20.02.
- #570 Incorrect display of curve widths in Solaris when changing curve basis.

## [6.0.4.1] - 2020-10-01

### Enhancements

#### Build

- **Custom Build Directory**: `BUILD_DIR` can now be used to overwrite the build directory for scons builds. (#490)
- **New Scons Version**: The build is now using Scons-3.1.2. (#549)

### Bugfixes

#### Build

- #533 Add BOOST_ALL_NO_LIB when building on windows.
- #501 The render delegate fails to build on gcc-4.8.5/Linux.
- #498 Can't build for Katana on Windows.

#### Procedural

- #513 Viewport representation in points mode doesn't have the correct matrix.

#### Render Delegate

- #488 Render Settings are not passed to the Render Delegate when using Husk.
- #518 HdArnold does not correctly handle texture coordinates when the primvar is not name `st` and `varname` in `PrimvarReader_float2` is of type `string`.
- #530 Cylinder light not matching the viewport preview.

## [6.0.4.0] - 2020-08-05

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

## [6.0.3.1] - 2020-06-04

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

## [6.0.3.0] - 2020-04-20

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

## [6.0.2.1] - 2020-03-11

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
