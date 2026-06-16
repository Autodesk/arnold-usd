// SPDX-License-Identifier: Apache-2.0
#include <pxr/pxr.h>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/arch/defines.h>
#include <pxr/base/arch/env.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/pxrCLI11/CLI11.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/tokens.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdRender/var.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_CLI;

namespace {

static const double _kPi = 3.14159265358979323846;

struct Args {
    std::string input;
    std::string output    = "turntable.usda";
    int         frames    = 30;
    int         width     = 1280;
    int         height    = 720;
    std::string hdri;
    std::string hdriDir;
    std::string lightRig  = "auto";
    double      lightIntensity = 1.0; // Multiplier applied to every rig's light intensity.
    bool        listLightRigs = false;
    std::string upAxis;   // empty = auto-detect from stage
    double      cameraHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      targetHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      cameraZoom   = 1.0; // Multiplicative factor for camera orbit distance.
    std::string mode         = "camera"; // camera (default), object, or light
    std::string studioSets;  // Comma-separated studio set names
    bool        listStudioSets = false;
    std::vector<float> backgroundColor; // empty = no background; otherwise {R, G, B}
};

struct LightRig {
    std::string name;
    std::string hdri;
    bool        isTwoQuad = false;
    bool        isNoLight = false;
};

enum class TurntableMode {
    RotateCamera,
    RotateObject,
    RotateLight
};

bool _ParseMode(const std::string &modeString, TurntableMode &out)
{
    const std::string mode = TfStringToLower(modeString);
    if (mode == "camera") {
        out = TurntableMode::RotateCamera;
        return true;
    }
    if (mode == "object") {
        out = TurntableMode::RotateObject;
        return true;
    }
    if (mode == "light") {
        out = TurntableMode::RotateLight;
        return true;
    }
    return false;
}

const char *_ModeToString(TurntableMode mode)
{
    switch (mode) {
    case TurntableMode::RotateCamera: return "camera";
    case TurntableMode::RotateObject: return "object";
    case TurntableMode::RotateLight: return "light";
    }
    return "camera";
}

enum class StudioSetType {
    Ground,
    Cyclo
};

bool _ParseStudioSet(const std::string &studioSetString, StudioSetType &out)
{
    const std::string studioSet = TfStringToLower(studioSetString);
    if (studioSet == "pedestal") {
        out = StudioSetType::Ground;
        return true;
    }
    if (studioSet == "cyclo" || studioSet == "cyc" || studioSet == "cyclorama") {
        out = StudioSetType::Cyclo;
        return true;
    }
    return false;
}

const char *_StudioSetToString(StudioSetType studioSet)
{
    switch (studioSet) {
    case StudioSetType::Ground: return "pedestal";
    case StudioSetType::Cyclo: return "cyclo";
    }
    return "pedestal";
}

struct StudioSetInfo {
    StudioSetType type;
    const char   *aliases;
    const char   *description;
};

// Studio sets selectable through --studio-set, in display order.
const std::vector<StudioSetInfo> &_AvailableStudioSets()
{
    static const std::vector<StudioSetInfo> studioSets = {
        {StudioSetType::Ground, "pedestal",
         "Cylindrical pedestal under the asset"},
        {StudioSetType::Cyclo, "cyclo, cyc, cyclorama",
         "Curved cyclorama wall and floor enclosing the asset"},
    };
    return studioSets;
}

GfMatrix4d _RotationMatrixForUpAxis(const TfToken &upAxis, double angleDegrees)
{
    const GfVec3d axis = (upAxis == UsdGeomTokens->z) ? GfVec3d(0.0, 0.0, 1.0) : GfVec3d(0.0, 1.0, 0.0);
    GfMatrix4d rotation(1.0);
    rotation.SetRotate(GfRotation(axis, angleDegrees));
    return rotation;
}

GfMatrix4d _RotationAroundPivot(const TfToken &upAxis, double angleDegrees, const GfVec3d &pivot)
{
    GfMatrix4d toOrigin(1.0);
    GfMatrix4d backToPivot(1.0);
    toOrigin.SetTranslate(-pivot);
    backToPivot.SetTranslate(pivot);
    return toOrigin * _RotationMatrixForUpAxis(upAxis, angleDegrees) * backToPivot;
}

void Configure(CLI::App *app, Args &args)
{
    app->add_option("-o,--output", args.output,
        "Output USD file for the turntable scene (default: turntable.usda)")
        ->option_text("FILE");

    app->add_option("-n,--frames", args.frames,
        "Number of frames for a full 360 rotation (default: 30)")
        ->option_text("N");

    app->add_option("--width", args.width,
        "Render width in pixels (default: 1280)")
        ->option_text("PX");

    app->add_option("--height", args.height,
        "Render height in pixels (default: 720)")
        ->option_text("PX");

    app->add_option("--hdri", args.hdri,
        "Path to an HDRI texture for a single custom dome rig (overrides --hdri-dir discovery)")
        ->option_text("FILE");

    app->add_option("--hdri-dir", args.hdriDir,
        "Directory scanned for .exr, .hdr, .tif and .tx environment maps to generate dome light rigs")
        ->option_text("DIR");

    app->add_option("--light-rig", args.lightRig,
        "Light rig to select: auto, one discovered HDRI rig, two_quad, or no_light "
        "(ignored when --hdri is given, which forces the custom HDRI rig)")
        ->option_text("NAME");

    app->add_option("--light-intensity", args.lightIntensity,
        "Multiplier applied to the selected light rig's intensity (default: 1.0, must be >= 0)")
        ->option_text("MULT");

    app->add_flag("--list-light-rigs", args.listLightRigs,
        "List discovered light rig names and exit");

    app->add_option("--up", args.upAxis,
        "Up axis override: Y or Z (default: read from asset stage)")
        ->option_text("AXIS");

    app->add_option("--camera-height", args.cameraHeight,
        "Camera height relative to bbox up-axis extent (0=bottom, 1=top, values outside [0,1] allowed, default: 0.5)")
        ->option_text("T");

    app->add_option("--target-height", args.targetHeight,
        "Camera target height relative to bbox up-axis extent (0=bottom, 1=top, values outside [0,1] allowed, default: 0.5)")
        ->option_text("T");

    app->add_option("--camera-zoom", args.cameraZoom,
        "Camera zoom factor (default: 1.0, >1 zooms in, <1 zooms out, must be > 0)")
        ->option_text("S");

    app->add_option("--mode", args.mode,
        "Animation mode: camera (default), object, or light")
        ->option_text("MODE");

    app->add_option("--studio-set", args.studioSets,
        "Add studio sets to the generated stage. Supported values: pedestal, cyclo (comma-separated)")
        ->option_text("NAME");

    app->add_flag("--list-studio-sets", args.listStudioSets,
        "List available studio set names and exit");

    app->add_option("--background-color", args.backgroundColor,
        "Solid background color as 3 linear floats: R G B (default: no background)")
        ->expected(3)->option_text("R G B");

    app->add_option("input", args.input, "USD asset file to turntable")
        ->option_text("FILE");
}

struct AssetBounds {
    GfVec3d center;
    GfVec3d size;
    double  radius;  // half diagonal of the aligned bounding box
    double  bottomFaceDiagonal;
    double  upMin;
    double  upMax;
    TfToken upAxis;
};

int _GetUpAxisIndex(const TfToken &upAxis)
{
    return upAxis == UsdGeomTokens->z ? 2 : 1;
}

double _Lerp01(double t, double minValue, double maxValue)
{
    return minValue + t * (maxValue - minValue);
}

bool _DirectoryExists(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR) != 0;
}

std::string _NormalizePath(const std::string &path)
{
    if (path.empty()) {
        return path;
    }
    return TfNormPath(path);
}

std::string _GetExecutablePath()
{
    return _NormalizePath(ArchGetExecutablePath());
}

std::vector<std::string> _GetHdriSearchDirs(const Args &args)
{
    std::vector<std::string> directories;
    std::set<std::string> seen;

    const auto addDirectory = [&](const std::string &directory) {
        if (directory.empty()) {
            return;
        }

        const std::string normalized = _NormalizePath(directory);
        if (seen.insert(normalized).second) {
            directories.push_back(normalized);
        }
    };

    if (!args.hdriDir.empty()) {
        addDirectory(args.hdriDir);
    } else {
        addDirectory(TfStringCatPaths(TfGetPathName(_GetExecutablePath()), "hdri"));
    }

    const std::string envDirs = ArchGetEnv("ARNOLD_TURNTABLE_HDRI_DIRS");
    if (!envDirs.empty()) {
        const std::vector<std::string> splitDirs = TfStringSplit(envDirs, ARCH_PATH_LIST_SEP);
        for (const std::string &directory : splitDirs) {
            addDirectory(directory);
        }
    }

    return directories;
}

// Returns true if the file name has an extension we recognize as an HDRI
// environment map (.exr, .hdr, .tif, .tiff, .tx). Comparison is case-insensitive.
bool _IsHdriFile(const std::string &fileName)
{
    const std::string suffix = TfStringToLower(TfStringGetSuffix(fileName));
    return suffix == "exr" || suffix == "hdr" || suffix == "tif" || suffix == "tiff" || suffix == "tx";
}

// True if the file is a tiled/mip-mapped .tx texture.
bool _IsTxFile(const std::string &fileName)
{
    return TfStringToLower(TfStringGetSuffix(fileName)) == "tx";
}

// Reduces a file name to a stem shared by a source map and its .tx counterpart
// when the .tx was produced by simply appending ".tx" (e.g. "env.exr" and
// "env.exr.tx" both yield "env"). Stems are compared exactly: colorspace or
// other naming differences (e.g. "env_acescg_raw.exr.tx") do not match the
// source and are kept as distinct rigs.
std::string _HdriStem(const std::string &fileName)
{
    std::string stem = fileName;
    if (_IsTxFile(stem)) {
        stem = TfStringGetBeforeSuffix(stem);
    }
    stem = TfStringGetBeforeSuffix(stem);  // drop the image extension (.exr/.hdr/.tif)
    return stem;
}

std::vector<std::string> _ListHdriFiles(const std::string &directory)
{
    std::vector<std::string> files;

#ifdef _WIN32
    const std::string pattern = TfStringCatPaths(directory, "*.*");
    WIN32_FIND_DATAA fileData;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &fileData);
    if (handle == INVALID_HANDLE_VALUE) {
        return files;
    }

    do {
        if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::string fileName = fileData.cFileName;
        if (_IsHdriFile(fileName)) {
            files.push_back(fileName);
        }
    } while (FindNextFileA(handle, &fileData) != 0);
    FindClose(handle);
#else
    DIR *dir = opendir(directory.c_str());
    if (!dir) {
        return files;
    }

    while (const dirent *entry = readdir(dir)) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") {
            continue;
        }
        if (_IsHdriFile(fileName)) {
            files.push_back(fileName);
        }
    }
    closedir(dir);
#endif

    std::sort(files.begin(), files.end());
    return files;
}

std::string _SanitizeRigName(const std::string &name)
{
    std::string out;
    out.reserve(name.size());

    bool prevUnderscore = false;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            prevUnderscore = false;
        } else if (!prevUnderscore) {
            out.push_back('_');
            prevUnderscore = true;
        }
    }

    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }

    if (out.empty()) {
        out = "rig";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out = "hdri_" + out;
    }
    return out;
}

std::vector<LightRig> _CollectLightRigs(const Args &args)
{
    std::vector<LightRig> rigs;
    std::set<std::string> usedNames;

    // Add white domelight as default
    LightRig whiteDomeRig;
    whiteDomeRig.name = "white_dome";
    whiteDomeRig.hdri = "";  // No HDRI texture, use white color
    whiteDomeRig.isTwoQuad = false;
    rigs.push_back(whiteDomeRig);
    usedNames.insert("white_dome");

    if (!args.hdri.empty()) {
        LightRig customRig;
        customRig.name = "custom_hdri";
        customRig.hdri = args.hdri;
        customRig.isTwoQuad = false;
        rigs.push_back(customRig);
        usedNames.insert("custom_hdri");
    } else {
        const std::vector<std::string> hdriDirs = _GetHdriSearchDirs(args);
        for (const std::string &hdriDir : hdriDirs) {
            if (!_DirectoryExists(hdriDir)) {
                continue;
            }

            const std::vector<std::string> files = _ListHdriFiles(hdriDir);

            // Group files by their stem so a source map (e.g. env.exr) and its
            // optimized .tx counterpart (env.exr.tx) collapse into a single rig.
            // Prefer the .tx variant: it is generally generated from the source
            // map and loads faster in Arnold. Files whose stems differ (e.g. a
            // colorspace-tagged env_acescg_raw.exr.tx) stay as separate rigs.
            std::map<std::string, std::string> chosenByStem;
            for (const std::string &fileName : files) {
                const std::string stem = _HdriStem(fileName);
                auto it = chosenByStem.find(stem);
                if (it == chosenByStem.end()) {
                    chosenByStem[stem] = fileName;
                } else if (_IsTxFile(fileName) && !_IsTxFile(it->second)) {
                    it->second = fileName;
                }
            }

            for (const auto &entry : chosenByStem) {
                const std::string &stem = entry.first;
                const std::string &fileName = entry.second;
                std::string rigName = _SanitizeRigName(stem);
                if (usedNames.find(rigName) != usedNames.end()) {
                    int suffix = 2;
                    while (usedNames.find(rigName + "_" + std::to_string(suffix)) != usedNames.end()) {
                        ++suffix;
                    }
                    rigName = rigName + "_" + std::to_string(suffix);
                }
                LightRig hdriRig;
                hdriRig.name = rigName;
                hdriRig.hdri = TfStringCatPaths(hdriDir, fileName);
                hdriRig.isTwoQuad = false;
                rigs.push_back(hdriRig);
                usedNames.insert(rigName);
            }
        }
    }

    LightRig twoQuadRig;
    twoQuadRig.name = "two_quad";
    twoQuadRig.isTwoQuad = true;
    rigs.push_back(twoQuadRig);

    LightRig noLightRig;
    noLightRig.name = "no_light";
    noLightRig.isNoLight = true;
    rigs.push_back(noLightRig);
    return rigs;
}

const LightRig *_FindRigByName(const std::vector<LightRig> &rigs, const std::string &name)
{
    const std::string query = TfStringToLower(name);
    for (const LightRig &rig : rigs) {
        if (TfStringToLower(rig.name) == query) {
            return &rig;
        }
    }
    return nullptr;
}

void _ComputeCameraEyeAndTarget(const Args &args, const AssetBounds &bounds,
                                GfVec3d &eye, GfVec3d &target, GfVec3d &up)
{
    const double camDistance = bounds.radius * 3.0 / args.cameraZoom;
    const double cameraHeight = _Lerp01(args.cameraHeight, bounds.upMin, bounds.upMax);
    const double targetHeight = _Lerp01(args.targetHeight, bounds.upMin, bounds.upMax);

    up = bounds.upAxis == UsdGeomTokens->z ? GfVec3d(0.0, 0.0, 1.0) : GfVec3d(0.0, 1.0, 0.0);

    if (bounds.upAxis == UsdGeomTokens->z) {
        eye = GfVec3d(bounds.center[0] + camDistance, bounds.center[1], cameraHeight);
        target = GfVec3d(bounds.center[0], bounds.center[1], targetHeight);
    } else {
        eye = GfVec3d(bounds.center[0], cameraHeight, bounds.center[2] + camDistance);
        target = GfVec3d(bounds.center[0], targetHeight, bounds.center[2]);
    }
}

void _SetLookAtTransform(UsdGeomXformable xformable, const GfVec3d &position,
                         const GfVec3d &target, const GfVec3d &up)
{
    GfVec3d forward = target - position;
    if (forward.GetLengthSq() < 1e-12) {
        forward = GfVec3d(0.0, 0.0, -1.0);
    }
    forward.Normalize();

    GfVec3d right = GfCross(forward, up);
    if (right.GetLengthSq() < 1e-12) {
        right = GfVec3d(1.0, 0.0, 0.0);
    }
    right.Normalize();
    const GfVec3d lightUp = GfCross(right, forward).GetNormalized();

    const GfMatrix4d xform(
        right[0],       right[1],       right[2],       0.0,
        lightUp[0],     lightUp[1],     lightUp[2],     0.0,
        -forward[0],    -forward[1],    -forward[2],    0.0,
        position[0],    position[1],    position[2],    1.0);

    UsdGeomXformOp op = xformable.MakeMatrixXform();
    op.Set(xform);
}

float _ComputeQuadLightIntensity(double distance, double width, double height)
{
    // Keep illuminance roughly stable across assets by scaling intensity with distance^2 / area.
    const double area = std::max(width * height, 1e-6);
    const double distanceSquared = std::max(distance * distance, 1e-6);
    const double intensity = 10.0 * distanceSquared / area;
    return static_cast<float>(std::max(0.001, std::min(intensity, 10000.0)));
}

// Returns false and prints an error if the stage cannot be opened or the bbox
// is empty / degenerate.
bool ComputeAssetBounds(const std::string &assetPath, const std::string &upAxisOverride,
                        AssetBounds &out)
{
    UsdStageRefPtr stage = UsdStage::Open(assetPath);
    if (!stage) {
        fprintf(stderr, "turntable: cannot open '%s'\n", assetPath.c_str());
        return false;
    }

    if (!upAxisOverride.empty()) {
        if (upAxisOverride == "Y" || upAxisOverride == "y") {
            out.upAxis = UsdGeomTokens->y;
        } else if (upAxisOverride == "Z" || upAxisOverride == "z") {
            out.upAxis = UsdGeomTokens->z;
        } else {
            fprintf(stderr, "turntable: unknown up axis '%s' (expected Y or Z)\n",
                    upAxisOverride.c_str());
            return false;
        }
    } else {
        out.upAxis = UsdGeomGetStageUpAxis(stage);
        if (out.upAxis.IsEmpty()) {
            out.upAxis = UsdGeomTokens->y;
        }
    }

    // Include default and render purposes to capture the full visual extent.
    UsdGeomBBoxCache bboxCache(
        UsdTimeCode::Default(),
        {UsdGeomTokens->default_, UsdGeomTokens->render},
        /*useExtentsHint=*/true);

    // Union the world bound of every Boundable prim individually rather than
    // querying ComputeWorldBound() once on the pseudo-root. UsdGeomBBoxCache
    // treats a Boundable gprim as a leaf: it returns that prim's authored extent
    // and does not descend into any Boundable children it may have. Some DCC
    // exports (e.g. Cinema 4D) author geometry as nested Mesh-in-Mesh
    // hierarchies, so a single root-level query collapses the whole asset down to
    // the topmost mesh's extent — yielding a far-too-small bbox that makes
    // --camera-height appear to have no effect. Visiting each Boundable (instance
    // proxies included) and unioning their bounds captures the full hierarchy.
    GfRange3d range;
    for (const UsdPrim &prim : stage->Traverse(UsdTraverseInstanceProxies(UsdPrimDefaultPredicate))) {
        if (!prim.IsA<UsdGeomBoundable>()) {
            continue;
        }
        const GfRange3d primRange = bboxCache.ComputeWorldBound(prim).ComputeAlignedRange();
        if (!primRange.IsEmpty()) {
            range.UnionWith(primRange);
        }
    }

    if (range.IsEmpty()) {
        fprintf(stderr, "turntable: bounding box of '%s' is empty — "
                "no renderable geometry found\n", assetPath.c_str());
        return false;
    }

    out.center = range.GetMidpoint();
    out.size   = range.GetSize();
    out.radius = range.GetSize().GetLength() * 0.5;
    const int upAxisIndex = _GetUpAxisIndex(out.upAxis);
    const int axisAIndex  = 0;
    const int axisBIndex  = upAxisIndex == 1 ? 2 : 1;
    out.bottomFaceDiagonal = std::sqrt(
        out.size[axisAIndex] * out.size[axisAIndex] + out.size[axisBIndex] * out.size[axisBIndex]);
    out.upMin = range.GetMin()[upAxisIndex];
    out.upMax = range.GetMax()[upAxisIndex];
    return true;
}

static UsdShadeMaterial _CreateGroundMaterial(const UsdStageRefPtr &stage)
{
    const SdfPath materialPath("/__turntable/materials/pedestal");
    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
    UsdShadeShader shader = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("previewSurface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
        .Set(GfVec3f(0.18f, 0.18f, 0.18f));
    shader.CreateInput(TfToken("specularColor"), SdfValueTypeNames->Color3f)
        .Set(GfVec3f(0.0f, 0.0f, 0.0f));
    shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float)
        .Set(1.0f);
    shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float)
        .Set(0.0f);
    material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), TfToken("surface"));
    return material;
}

static GfVec3f _MakeCycloPoint(const GfVec3d &center, const TfToken &upAxis,
                               double angle, double radius, double upValue)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    if (upAxis == UsdGeomTokens->z) {
        return GfVec3f(static_cast<float>(center[0] + radius * c),
                       static_cast<float>(center[1] + radius * s),
                       static_cast<float>(upValue));
    }

    return GfVec3f(static_cast<float>(center[0] + radius * c),
                   static_cast<float>(upValue),
                   static_cast<float>(center[2] + radius * s));
}

static bool _RayIntersectCycloRadius(const GfVec3d &origin, const GfVec3d &direction,
                                     const TfToken &upAxis, const GfVec3d &center,
                                     double radius, double &tHit)
{
    const int lateralA = 0;
    const int lateralB = upAxis == UsdGeomTokens->z ? 1 : 2;

    const double ox = origin[lateralA] - center[lateralA];
    const double oy = origin[lateralB] - center[lateralB];
    const double dx = direction[lateralA];
    const double dy = direction[lateralB];

    const double a = dx * dx + dy * dy;
    if (a < 1e-12) {
        return false;
    }

    const double b = 2.0 * (ox * dx + oy * dy);
    const double c = ox * ox + oy * oy - radius * radius;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) {
        return false;
    }

    const double sqrtDisc = std::sqrt(discriminant);
    const double invDenom = 1.0 / (2.0 * a);
    const double t0 = (-b - sqrtDisc) * invDenom;
    const double t1 = (-b + sqrtDisc) * invDenom;

    const double eps = 1e-9;
    const bool t0Valid = t0 > eps;
    const bool t1Valid = t1 > eps;
    if (!t0Valid && !t1Valid) {
        return false;
    }

    tHit = t0Valid ? t0 : t1;
    if (t1Valid) {
        tHit = std::min(tHit, t1);
    }
    return true;
}

static double _ComputeCycloWallTopForCameraFrame(const AssetBounds &bounds, const Args &args,
                                                  double cycloRadius, double minWallTop)
{
    GfVec3d eye;
    GfVec3d target;
    GfVec3d up;
    _ComputeCameraEyeAndTarget(args, bounds, eye, target, up);

    GfVec3d forward = target - eye;
    if (forward.GetLengthSq() < 1e-12) {
        forward = bounds.upAxis == UsdGeomTokens->z ? GfVec3d(0.0, 0.0, -1.0) : GfVec3d(0.0, 0.0, -1.0);
    }
    forward.Normalize();

    GfVec3d right = GfCross(forward, up);
    if (right.GetLengthSq() < 1e-12) {
        right = GfVec3d(1.0, 0.0, 0.0);
    }
    right.Normalize();
    const GfVec3d camUp = GfCross(right, forward).GetNormalized();

    const double tanHalfH = (0.5 * 36.0) / 35.0;
    const double tanHalfV = tanHalfH * (static_cast<double>(args.height) / static_cast<double>(args.width));
    const double overscan = 1.2;
    const double tanHalfHOverscan = tanHalfH * overscan;
    const double tanHalfVOverscan = tanHalfV * overscan;

    double maxHitUp = minWallTop;
    const int upAxisIndex = _GetUpAxisIndex(bounds.upAxis);
    const double cameraDistance = bounds.radius * 3.0 / args.cameraZoom;
    const double wallTopMargin = std::max({(bounds.upMax - bounds.upMin) * 0.2, cameraDistance * 0.08, 0.001});
    bool anyHit = false;

    for (int sx = -1; sx <= 1; sx += 2) {
        GfVec3d rayDir = forward + right * (tanHalfHOverscan * static_cast<double>(sx)) + camUp * tanHalfVOverscan;
        if (rayDir.GetLengthSq() < 1e-12) {
            continue;
        }
        rayDir.Normalize();

        double tHit = 0.0;
        if (!_RayIntersectCycloRadius(eye, rayDir, bounds.upAxis, bounds.center, cycloRadius, tHit)) {
            continue;
        }

        const double hitUp = eye[upAxisIndex] + tHit * rayDir[upAxisIndex];
        maxHitUp = std::max(maxHitUp, hitUp + wallTopMargin);
        anyHit = true;
    }

    if (!anyHit) {
        const double conservativeTop = eye[upAxisIndex] +
            (cameraDistance + cycloRadius) * tanHalfVOverscan + wallTopMargin;
        maxHitUp = std::max(maxHitUp, conservativeTop);
    }

    return maxHitUp;
}

static bool _CreateCyclo(const UsdStageRefPtr &stage, const AssetBounds &bounds,
                         const Args &args, const UsdShadeMaterial &material)
{
    const double assetHeight = std::max(bounds.upMax - bounds.upMin, 1e-4);
    const double bboxRadius = bounds.bottomFaceDiagonal * 0.5;
    const double bboxMargin = std::max({assetHeight * 0.05, bounds.bottomFaceDiagonal * 0.01, 0.001});
    const double minBBoxRadius = bboxRadius + bboxMargin;

    const double cameraDistance = bounds.radius * 3.0 / args.cameraZoom;
    const double cameraClearance = std::max(cameraDistance * 0.05, 0.001);
    const double minCameraRadius = cameraDistance + cameraClearance;

    // Keep the cyclo larger than both the asset footprint and camera orbit
    // so the wall remains behind the camera-to-asset line of sight.
    const double cycloRadius = std::max(minBBoxRadius, minCameraRadius);
    const double gap = std::max(bounds.bottomFaceDiagonal * 0.001, 0.0001);
    const double floorLevel = bounds.upMin - gap;
    const double minWallTop = bounds.upMax + std::max(assetHeight * 0.1, 0.001);
    const double wallTop = _ComputeCycloWallTopForCameraFrame(bounds, args, cycloRadius, minWallTop);

    const double coveRadius = std::max(
        0.001,
        std::min(std::max(assetHeight * 0.3, cycloRadius * 0.15), cycloRadius * 0.5));
    const double floorToCoveRadius = std::max(0.001, cycloRadius - coveRadius);

    const int radialSegments = 64;
    const int coveSegments = 20;
    const int wallSegments = 8;

    std::vector<GfVec2d> profile;
    profile.reserve(static_cast<size_t>(2 + coveSegments + wallSegments));
    profile.push_back(GfVec2d(floorToCoveRadius, floorLevel));

    for (int i = 1; i <= coveSegments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(coveSegments);
        const double theta = t * (_kPi * 0.5);
        const double r = floorToCoveRadius + coveRadius * std::sin(theta);
        const double u = floorLevel + coveRadius * (1.0 - std::cos(theta));
        profile.push_back(GfVec2d(r, u));
    }

    const double wallStart = floorLevel + coveRadius;
    for (int i = 1; i <= wallSegments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(wallSegments);
        const double u = wallStart + (wallTop - wallStart) * t;
        profile.push_back(GfVec2d(cycloRadius, u));
    }

    const auto ringPointIndex = [radialSegments](size_t ring, int segment) -> int {
        return 1 + static_cast<int>(ring) * radialSegments + segment;
    };

    VtArray<GfVec3f> points;
    points.reserve(1 + profile.size() * static_cast<size_t>(radialSegments));
    points.push_back(_MakeCycloPoint(bounds.center, bounds.upAxis, 0.0, 0.0, floorLevel));

    for (size_t ring = 0; ring < profile.size(); ++ring) {
        const double radius = profile[ring][0];
        const double upValue = profile[ring][1];
        for (int segment = 0; segment < radialSegments; ++segment) {
            const double angle = (2.0 * _kPi * static_cast<double>(segment)) / static_cast<double>(radialSegments);
            points.push_back(_MakeCycloPoint(bounds.center, bounds.upAxis, angle, radius, upValue));
        }
    }

    VtArray<int> faceVertexCounts;
    VtArray<int> faceVertexIndices;

    for (int segment = 0; segment < radialSegments; ++segment) {
        const int nextSegment = (segment + 1) % radialSegments;
        faceVertexCounts.push_back(3);
        faceVertexIndices.push_back(0);
        faceVertexIndices.push_back(ringPointIndex(0, nextSegment));
        faceVertexIndices.push_back(ringPointIndex(0, segment));
    }

    for (size_t ring = 0; ring + 1 < profile.size(); ++ring) {
        for (int segment = 0; segment < radialSegments; ++segment) {
            const int nextSegment = (segment + 1) % radialSegments;
            const int a = ringPointIndex(ring, segment);
            const int b = ringPointIndex(ring, nextSegment);
            const int c = ringPointIndex(ring + 1, nextSegment);
            const int d = ringPointIndex(ring + 1, segment);

            faceVertexCounts.push_back(3);
            faceVertexIndices.push_back(a);
            faceVertexIndices.push_back(b);
            faceVertexIndices.push_back(c);

            faceVertexCounts.push_back(3);
            faceVertexIndices.push_back(a);
            faceVertexIndices.push_back(c);
            faceVertexIndices.push_back(d);
        }
    }

    UsdGeomMesh cyclo = UsdGeomMesh::Define(stage, SdfPath("/__turntable/studioSet/cyclo"));
    cyclo.CreatePointsAttr().Set(points);
    cyclo.CreateFaceVertexCountsAttr().Set(faceVertexCounts);
    cyclo.CreateFaceVertexIndicesAttr().Set(faceVertexIndices);
    cyclo.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    cyclo.CreateDoubleSidedAttr().Set(true);
    cyclo.CreateDisplayColorAttr().Set(VtArray<GfVec3f>{GfVec3f(0.18f, 0.18f, 0.18f)});
    UsdShadeMaterialBindingAPI::Apply(cyclo.GetPrim()).Bind(material);
    return true;
}

static bool _SetupStudioSets(const UsdStageRefPtr &stage, const std::vector<StudioSetType> &studioSets,
                        const AssetBounds &bounds, const Args &args)
{
    UsdGeomXform sceneStudioSet = UsdGeomXform::Define(stage, SdfPath("/__turntable/studioSet"));
    (void)sceneStudioSet;
    const UsdShadeMaterial groundMaterial = _CreateGroundMaterial(stage);
    const std::set<StudioSetType> activeStudioSets(studioSets.begin(), studioSets.end());

    const auto _IsStudioSetActive = [&activeStudioSets](StudioSetType studioSet) {
        return activeStudioSets.find(studioSet) != activeStudioSets.end();
    };

    {
        const double thickness = std::max(bounds.bottomFaceDiagonal * 0.01, 0.001);
        const double gap = std::max(bounds.bottomFaceDiagonal * 0.001, 0.0001);
        const double groundHeight = bounds.upMin - gap - (thickness * 0.5);

        UsdGeomCylinder ground = UsdGeomCylinder::Define(stage, SdfPath("/__turntable/studioSet/pedestal"));
        ground.CreateAxisAttr().Set(bounds.upAxis == UsdGeomTokens->z ? UsdGeomTokens->z : UsdGeomTokens->y);
        ground.CreateRadiusAttr().Set(bounds.bottomFaceDiagonal);
        ground.CreateHeightAttr().Set(thickness);
        ground.CreateDisplayColorAttr().Set(VtArray<GfVec3f>{GfVec3f(0.18f, 0.18f, 0.18f)});
        UsdShadeMaterialBindingAPI::Apply(ground.GetPrim()).Bind(groundMaterial);

        UsdGeomXformable xformable(ground.GetPrim());
        UsdGeomXformOp translateOp = xformable.AddTranslateOp();
        GfVec3d groundCenter = bounds.center;
        groundCenter[_GetUpAxisIndex(bounds.upAxis)] = groundHeight;
        translateOp.Set(groundCenter);

        ground.GetPrim().SetActive(_IsStudioSetActive(StudioSetType::Ground));
    }

    if (!_CreateCyclo(stage, bounds, args, groundMaterial)) {
        return false;
    }
    stage->GetPrimAtPath(SdfPath("/__turntable/studioSet/cyclo")).SetActive(_IsStudioSetActive(StudioSetType::Cyclo));

    return true;
}

// Sublayer the input asset into the root layer. Unlike a reference, a sublayer
// composes the asset's full root hierarchy even when it declares no default prim
// (and never goes stale against a renamed default prim). It is appended as the
// weakest sublayer so the turntable prims authored in the root layer win.
static void _SetupAsset(const UsdStageRefPtr &stage, const std::string &assetPath)
{
    stage->GetRootLayer()->InsertSubLayerPath(TfAbsPath(assetPath));
}

// /__turntable  —  scaffolding root grouping the camera, lights and studio sets.
// The unlikely-to-clash name keeps these prims namespaced away from the
// sublayered asset's root prims. In object mode the asset stays fixed and this
// group counter-rotates, which is visually identical to spinning the asset: the
// camera and lights orbit together, so the lighting sweeps across the asset
// exactly as it did when the asset itself was rotated.
static void _SetupTurntableRoot(const UsdStageRefPtr &stage, TurntableMode mode,
                                const AssetBounds &bounds, int frames)
{
    UsdGeomXform root = UsdGeomXform::Define(stage, SdfPath("/__turntable"));

    if (mode != TurntableMode::RotateObject) {
        return;
    }

    UsdGeomXformOp matOp = root.AddTransformOp();
    for (int i = 0; i < frames; ++i) {
        const double angleDegrees = -360.0 * double(i) / double(frames);
        matOp.Set(_RotationAroundPivot(bounds.upAxis, angleDegrees, bounds.center), UsdTimeCode(double(i)));
    }
}

// Returns false if the asset has no UsdRenderSettings prims.
// On success, outSettingsPath is the scene path of the chosen prim and
// outResolution is its authored resolution (defaulting to 0,0 if not set).
static bool _FindAssetRenderSettings(const std::string &assetPath,
                                     SdfPath &outSettingsPath,
                                     GfVec2i &outResolution)
{
    UsdStageRefPtr assetStage = UsdStage::Open(assetPath);
    if (!assetStage) {
        return false;
    }

    std::vector<UsdPrim> allSettings;
    for (const UsdPrim &prim : assetStage->Traverse()) {
        if (prim.IsA<UsdRenderSettings>()) {
            allSettings.push_back(prim);
        }
    }

    if (allSettings.empty()) {
        return false;
    }

    // Prefer the prim named in the asset's renderSettingsPrimPath metadata.
    UsdPrim chosen = allSettings.front();
    VtValue metaVal;
    if (assetStage->HasMetadata(UsdRenderTokens->renderSettingsPrimPath) &&
        assetStage->GetMetadata(UsdRenderTokens->renderSettingsPrimPath, &metaVal)) {
        const std::string metaPath = metaVal.IsHolding<std::string>()
            ? metaVal.UncheckedGet<std::string>()
            : metaVal.UncheckedGet<SdfPath>().GetString();
        for (const UsdPrim &prim : allSettings) {
            if (prim.GetPath().GetString() == metaPath) {
                chosen = prim;
                break;
            }
        }
    }

    printf("turntable: using asset render settings '%s'", chosen.GetPath().GetText());
    if (allSettings.size() > 1) {
        printf(" (%zu settings found in asset)", allSettings.size());
    }
    printf("\n");

    UsdRenderSettings settings(chosen);
    outSettingsPath = chosen.GetPath();
    settings.GetResolutionAttr().Get(&outResolution);
    return true;
}

// /camera  —  camera with a time-sampled look-at matrix that orbits the asset center
static SdfPath _SetupCamera(const UsdStageRefPtr &stage, const Args &args,
                            const AssetBounds &bounds, TurntableMode mode,
                            int effectiveWidth, int effectiveHeight)
{
    const SdfPath cameraPath("/__turntable/camera");
    const double camDistance = bounds.radius * 3.0 / args.cameraZoom;
    const double nearClip    = std::max(1.0, camDistance * 0.01);
    const double farClip     = camDistance * 10.0;

    UsdGeomCamera cam = UsdGeomCamera::Define(stage, cameraPath);

    // Single matrix xformOp animated over the full frame range.
    UsdGeomXformOp matOp = cam.MakeMatrixXform();

    const GfVec3d upVec = (bounds.upAxis == UsdGeomTokens->z)
        ? GfVec3d(0, 0, 1) : GfVec3d(0, 1, 0);
    const double cameraHeight = _Lerp01(args.cameraHeight, bounds.upMin, bounds.upMax);
    const double targetHeight = _Lerp01(args.targetHeight, bounds.upMin, bounds.upMax);

    const int sampleCount = (mode == TurntableMode::RotateCamera) ? args.frames : 1;
    for (int i = 0; i < sampleCount; ++i) {
        const double angle = (mode == TurntableMode::RotateCamera)
            ? (2.0 * _kPi * double(i) / double(args.frames))
            : 0.0;
        const double zUpAngle = angle - (_kPi * 0.5);

        // Orbit in the plane perpendicular to the up axis.
        GfVec3d eye;
        GfVec3d target;
        if (bounds.upAxis == UsdGeomTokens->z) {
            eye = GfVec3d(bounds.center[0] + camDistance * std::cos(zUpAngle),
                          bounds.center[1] + camDistance * std::sin(zUpAngle),
                          cameraHeight);
            target = GfVec3d(bounds.center[0], bounds.center[1], targetHeight);
        } else { // Y-up
            eye = GfVec3d(bounds.center[0] + camDistance * std::sin(angle),
                          cameraHeight,
                          bounds.center[2] + camDistance * std::cos(angle));
            target = GfVec3d(bounds.center[0], targetHeight, bounds.center[2]);
        }

        // Look-at toward a target at relative bbox height on the up axis.
        const GfVec3d forward = (target - eye).GetNormalized();
        const GfVec3d right   = GfCross(forward, upVec).GetNormalized();
        const GfVec3d camUp   = GfCross(right, forward);

        // Camera-to-world matrix in USD row-vector convention:
        // each row is a camera local axis expressed in world space.
        // USD cameras look along local -Z, so row 2 = -forward.
        const GfMatrix4d mat(
            right[0],    right[1],    right[2],    0.0,
            camUp[0],    camUp[1],    camUp[2],    0.0,
            -forward[0], -forward[1], -forward[2], 0.0,
            eye[0],      eye[1],      eye[2],      1.0);
        if (mode == TurntableMode::RotateCamera) {
            matOp.Set(mat, UsdTimeCode(double(i)));
        } else {
            matOp.Set(mat);
        }
    }

    // 35 mm lens on a 36 mm horizontal gate — a neutral lookdev focal length.
    const float haperture = 36.0f;
    const float vaperture = haperture * float(effectiveHeight) / float(effectiveWidth);
    cam.GetHorizontalApertureAttr().Set(haperture);
    cam.GetVerticalApertureAttr().Set(vaperture);
    cam.GetFocalLengthAttr().Set(35.0f);
    cam.GetClippingRangeAttr().Set(GfVec2f(float(nearClip), float(farClip)));

    return cameraPath;
}

// /lights/dome  —  dome light, optionally driven by an HDRI texture or color
static void _SetupDomeLight(const UsdStageRefPtr &stage, const SdfPath &lightPath,
                            const std::string &hdri, float intensity = 1.0f,
                            const GfVec3f &color = GfVec3f(1.0f))
{
    UsdLuxDomeLight dome = UsdLuxDomeLight::Define(stage, lightPath);
    UsdLuxLightAPI api = UsdLuxLightAPI::Apply(dome.GetPrim());
    api.CreateIntensityAttr().Set(intensity);
    api.CreateColorAttr().Set(color);
    dome.GetPrim()
        .CreateAttribute(TfToken("primvars:arnold:camera"), SdfValueTypeNames->Float, true)
        .Set(0.0f);
    if (!hdri.empty()) {
        dome.CreateTextureFileAttr().Set(SdfAssetPath(hdri));
        dome.CreateTextureFormatAttr().Set(UsdLuxTokens->latlong);

        // Match Arnold's latlong sampling orientation used by the turntable rigs.
        UsdGeomXformable xformable(dome.GetPrim());
        xformable.ClearXformOpOrder();
        xformable.AddRotateXOp(UsdGeomXformOp::PrecisionFloat).Set(90.0f);
    }
}

static void _SetupTwoQuadRig(const UsdStageRefPtr &stage, const AssetBounds &bounds,
                             const Args &args)
{
    GfVec3d eye;
    GfVec3d target;
    GfVec3d up;
    _ComputeCameraEyeAndTarget(args, bounds, eye, target, up);

    GfVec3d forward = target - eye;
    if (forward.GetLengthSq() < 1e-12) {
        forward = GfVec3d(0.0, 0.0, -1.0);
    }
    forward.Normalize();

    GfVec3d right = GfCross(forward, up);
    if (right.GetLengthSq() < 1e-12) {
        right = GfVec3d(1.0, 0.0, 0.0);
    }
    right.Normalize();

    const int upAxisIndex = _GetUpAxisIndex(bounds.upAxis);
    const int lateralAxisIndex = bounds.upAxis == UsdGeomTokens->z ? 1 : 0;
    const double width = std::max(bounds.size[lateralAxisIndex] * 2.0, 0.001);
    const double height = std::max(bounds.size[upAxisIndex] * 2.0, 0.001);
    
    // Position lights far enough from bbox to avoid intersection and shadow zones.
    // Use camera distance as reference so offset scales with asset size.
    const double camDistance = bounds.radius * 3.0 / args.cameraZoom;
    const double offset = camDistance * 0.6;

    GfVec3d center = bounds.center;
    center[upAxisIndex] = _Lerp01(args.cameraHeight, bounds.upMin, bounds.upMax);
    const GfVec3d leftPos = center - right * offset;
    const GfVec3d rightPos = center + right * offset;
    const float multiplier = static_cast<float>(args.lightIntensity);
    const float leftIntensity = multiplier * _ComputeQuadLightIntensity((target - leftPos).GetLength(), width, height);
    const float rightIntensity = multiplier * _ComputeQuadLightIntensity((target - rightPos).GetLength(), width, height);

    UsdLuxRectLight left = UsdLuxRectLight::Define(stage, SdfPath("/__turntable/lights/key_left"));
    UsdLuxLightAPI leftApi = UsdLuxLightAPI::Apply(left.GetPrim());
    leftApi.CreateIntensityAttr().Set(leftIntensity);
    leftApi.CreateSpecularAttr().Set(0.0f);
    left.CreateWidthAttr().Set(static_cast<float>(width));
    left.CreateHeightAttr().Set(static_cast<float>(height));
    _SetLookAtTransform(UsdGeomXformable(left.GetPrim()), leftPos, target, up);

    UsdLuxRectLight rightLight = UsdLuxRectLight::Define(stage, SdfPath("/__turntable/lights/key_right"));
    UsdLuxLightAPI rightApi = UsdLuxLightAPI::Apply(rightLight.GetPrim());
    rightApi.CreateIntensityAttr().Set(rightIntensity);
    rightApi.CreateSpecularAttr().Set(0.0f);
    rightLight.CreateWidthAttr().Set(static_cast<float>(width));
    rightLight.CreateHeightAttr().Set(static_cast<float>(height));
    _SetLookAtTransform(UsdGeomXformable(rightLight.GetPrim()), rightPos, target, up);
}

// Deactivate any lights authored in the sublayered asset so the turntable's own
// rig is the only illumination. The turntable's lights live under /__turntable
// and are left untouched. Mesh lights are deactivated too, which also prunes
// their emissive geometry. Returns the number of asset lights deactivated.
static int _DeactivateAssetLights(const UsdStageRefPtr &stage)
{
    static const SdfPath turntableRoot("/__turntable");

    std::vector<UsdPrim> assetLights;
    for (const UsdPrim &prim : stage->Traverse()) {
        if (prim.GetPath().HasPrefix(turntableRoot)) {
            continue;
        }
        if (prim.HasAPI<UsdLuxLightAPI>()) {
            assetLights.push_back(prim);
        }
    }

    for (const UsdPrim &light : assetLights) {
        light.SetActive(false);
    }
    return static_cast<int>(assetLights.size());
}

static void _SetupLights(const UsdStageRefPtr &stage, const Args &args,
                         const AssetBounds &bounds, TurntableMode mode, int frames,
                         const std::vector<LightRig> &rigs, const std::string &selectedRig)
{
    UsdGeomXform lights = UsdGeomXform::Define(stage, SdfPath("/__turntable/lights"));

    if (mode == TurntableMode::RotateLight) {
        UsdGeomXformOp matOp = lights.AddTransformOp();
        for (int i = 0; i < frames; ++i) {
            const double angleDegrees = 360.0 * double(i) / double(frames);
            matOp.Set(_RotationMatrixForUpAxis(bounds.upAxis, angleDegrees), UsdTimeCode(double(i)));
        }
    }

    UsdVariantSet rigVariantSet = lights.GetPrim().GetVariantSets().AddVariantSet("rig");
    for (const LightRig &rig : rigs) {
        rigVariantSet.AddVariant(rig.name);
        rigVariantSet.SetVariantSelection(rig.name);
        UsdEditContext variantContext(rigVariantSet.GetVariantEditContext());

        if (rig.isNoLight) {
            // Empty variant: no lights authored, user provides their own.
        } else if (rig.isTwoQuad) {
            _SetupTwoQuadRig(stage, bounds, args);
        } else {
            const float intensity = static_cast<float>(args.lightIntensity);
            // Use white color for white_dome rig, otherwise no color override
            if (rig.name == "white_dome") {
                _SetupDomeLight(stage, SdfPath("/__turntable/lights/dome"), rig.hdri, intensity,
                                GfVec3f(1.0f, 1.0f, 1.0f));
            } else {
                _SetupDomeLight(stage, SdfPath("/__turntable/lights/dome"), rig.hdri, intensity);
            }
        }
    }

    rigVariantSet.SetVariantSelection(selectedRig);
}

// /Render/TurntableBackground  —  ArnoldNodeGraph wrapping a flat-color shader,
// referenced from the render settings via arnold:global:background. Camera rays
// that miss geometry return this flat color.
static void _SetupBackground(const UsdStageRefPtr &stage, const UsdRenderSettings &settings,
                             const GfVec3f &color)
{
    const SdfPath graphPath("/Render/TurntableBackground");
    UsdPrim graphPrim = stage->DefinePrim(graphPath, TfToken("ArnoldNodeGraph"));
    // Belt-and-suspenders for the reader's node-graph lookup, which falls back to
    // matching primvars:arnold:name when the literal path does not resolve.
    graphPrim.CreateAttribute(TfToken("primvars:arnold:name"), SdfValueTypeNames->String, false)
        .Set(graphPath.GetString());

    UsdShadeShader flat = UsdShadeShader::Define(stage, graphPath.AppendChild(TfToken("flat")));
    flat.CreateIdAttr(VtValue(TfToken("arnold:flat")));
    flat.CreateInput(TfToken("color"), SdfValueTypeNames->Color3f).Set(color);
    UsdShadeOutput flatOut = flat.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);

    UsdShadeNodeGraph graph(graphPrim);
    UsdShadeOutput backgroundOut = graph.CreateOutput(TfToken("background"), SdfValueTypeNames->Token);
    backgroundOut.ConnectToSource(flatOut);

    settings.GetPrim()
        .CreateAttribute(TfToken("arnold:global:background"), SdfValueTypeNames->String, /*custom=*/true)
        .Set(graphPath.GetString());
}

// Build a product name with the frame number baked in. A run of '#' is replaced
// by the zero-padded frame (one digit per '#'); otherwise a 4-digit number is
// inserted before the extension. ("output.jpg", 0) -> "output.0000.jpg".
static std::string _ProductNameForFrame(const std::string &productName, int frame)
{
    const size_t hashStart = productName.find('#');
    if (hashStart != std::string::npos) {
        size_t hashEnd = hashStart;
        while (hashEnd < productName.size() && productName[hashEnd] == '#') {
            ++hashEnd;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%0*d", static_cast<int>(hashEnd - hashStart), frame);
        return productName.substr(0, hashStart) + buffer + productName.substr(hashEnd);
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d", frame);
    const std::string extension = TfGetExtension(productName);
    if (extension.empty()) {
        return productName + "." + buffer;
    }
    // Strip the extension and its dot, then re-append after the frame number.
    const std::string base = productName.substr(0, productName.size() - extension.size() - 1);
    return base + "." + buffer + "." + extension;
}

// Time-sample productName so each frame writes a distinct file. USD does not
// expand frame patterns (e.g. "####") in productName at render time, so we bake
// the frame number into a value authored at every timecode. The renderer reads
// productName at the timecode it renders and gets that frame's filename. The
// frame number matches the timecode (timecode 0 -> base.0000.ext).
static void _AuthorPerFrameProductName(const UsdRenderProduct &product,
                                       const std::string &baseName, int frames)
{
    UsdAttribute nameAttr = product.CreateProductNameAttr();
    // Default value (used by Default-time reads) plus a sample per frame; at a
    // given timecode the time sample wins over the default.
    nameAttr.Set(TfToken(_ProductNameForFrame(baseName, 0)));
    for (int i = 0; i < frames; ++i) {
        nameAttr.Set(TfToken(_ProductNameForFrame(baseName, i)), UsdTimeCode(double(i)));
    }
}

// Override an existing asset render settings prim with turntable values:
// camera, per-frame product names, and optionally background. The prim itself
// is already authored in the sublayered asset; we author an over to change only
// what we need.
static void _OverrideAssetRenderSettings(const UsdStageRefPtr &stage,
                                         const SdfPath &settingsPath,
                                         const SdfPath &cameraPath,
                                         const Args &args)
{
    UsdPrim settingsPrim = stage->OverridePrim(settingsPath);
    UsdRenderSettings settings(settingsPrim);
    settings.GetCameraRel().SetTargets({cameraPath});

    // The asset's render products keep their authored productName, a fixed
    // filename, so without per-frame names every frame overwrites the same file.
    // Re-author each product's name as a time sample per frame.
    SdfPathVector productTargets;
    settings.GetProductsRel().GetTargets(&productTargets);
    for (const SdfPath &productPath : productTargets) {
        UsdRenderProduct product(stage->GetPrimAtPath(productPath));
        if (!product) {
            continue;
        }
        TfToken productName;
        if (!product.GetProductNameAttr().Get(&productName) || productName.IsEmpty()) {
            continue;
        }
        // Authors time samples on the product in the root layer (the edit target).
        _AuthorPerFrameProductName(product, productName.GetString(), args.frames);
    }

    if (args.backgroundColor.size() == 3) {
        _SetupBackground(stage, settings,
            GfVec3f(args.backgroundColor[0], args.backgroundColor[1], args.backgroundColor[2]));
    }
}

// /Render  —  UsdRenderSettings + product + beauty AOV
static void _SetupRenderSettings(const UsdStageRefPtr &stage, const Args &args,
                                  const SdfPath &cameraPath)
{
    UsdGeomScope::Define(stage, SdfPath("/Render"));
    UsdRenderSettings settings = UsdRenderSettings::Define(stage, SdfPath("/Render/TurntableSettings"));
    settings.GetResolutionAttr().Set(GfVec2i(args.width, args.height));
    settings.GetCameraRel().SetTargets({cameraPath});

    // Output image sequence: strip the .usda extension and use a .exr base. USD
    // does not expand frame patterns in productName, so the name is time-sampled
    // per frame by _AuthorPerFrameProductName (e.g. turntable.0001.exr).
    const std::string productBase = TfStringGetBeforeSuffix(args.output) + ".exr";
    UsdRenderProduct product = UsdRenderProduct::Define(stage, SdfPath("/Render/TurntableProduct"));
    _AuthorPerFrameProductName(product, productBase, args.frames);
    product.GetProductTypeAttr().Set(TfToken("arnold"));
    settings.GetProductsRel().AddTarget(SdfPath("/Render/TurntableProduct"));

    // Beauty AOV (RGBA).
    UsdRenderVar beauty = UsdRenderVar::Define(stage, SdfPath("/Render/TurntableVars/beauty"));
    beauty.GetDataTypeAttr().Set(TfToken("color4f"));
    beauty.GetSourceNameAttr().Set(std::string("RGBA"));
    beauty.GetSourceTypeAttr().Set(UsdRenderTokens->raw);
    product.GetOrderedVarsRel().AddTarget(SdfPath("/Render/TurntableVars/beauty"));

    if (args.backgroundColor.size() == 3) {
        _SetupBackground(stage, settings,
            GfVec3f(args.backgroundColor[0], args.backgroundColor[1], args.backgroundColor[2]));
    }
}

int Run(const Args &args)
{
    if (args.input.empty() && !args.listLightRigs && !args.listStudioSets) {
        fprintf(stderr, "turntable: input is required unless --list-light-rigs "
                "or --list-studio-sets is used\n");
        return 1;
    }

    if (args.listStudioSets) {
        printf("Studio sets:\n");
        for (const StudioSetInfo &info : _AvailableStudioSets()) {
            printf("  %s [%s] - %s\n", _StudioSetToString(info.type), info.aliases, info.description);
        }
        return 0;
    }

    if (args.cameraZoom <= 0.0) {
        fprintf(stderr, "turntable: --camera-zoom must be > 0 (got %.4g)\n", args.cameraZoom);
        return 1;
    }

    if (args.width <= 0) {
        fprintf(stderr, "turntable: --width must be > 0 (got %d)\n", args.width);
        return 1;
    }

    if (args.height <= 0) {
        fprintf(stderr, "turntable: --height must be > 0 (got %d)\n", args.height);
        return 1;
    }

    if (args.frames <= 0) {
        fprintf(stderr, "turntable: --frames must be > 0 (got %d)\n", args.frames);
        return 1;
    }

    if (args.lightIntensity < 0.0) {
        fprintf(stderr, "turntable: --light-intensity must be >= 0 (got %.4g)\n", args.lightIntensity);
        return 1;
    }

    TurntableMode mode;
    if (!_ParseMode(args.mode, mode)) {
        fprintf(stderr, "turntable: unknown --mode '%s' (expected camera, object, or light)\n",
                args.mode.c_str());
        return 1;
    }

    std::vector<StudioSetType> studioSets;
    if (!args.studioSets.empty()) {
        // Split comma-separated studio set names
        std::vector<std::string> studioSetNames;
        std::string current;
        for (char c : args.studioSets) {
            if (c == ',') {
                if (!current.empty()) {
                    studioSetNames.push_back(current);
                    current.clear();
                }
            } else if (c != ' ') {  // Skip whitespace
                current += c;
            }
        }
        if (!current.empty()) {
            studioSetNames.push_back(current);
        }
        
        studioSets.reserve(studioSetNames.size());
        for (const std::string &studioSetString : studioSetNames) {
            StudioSetType studioSet;
            if (!_ParseStudioSet(studioSetString, studioSet)) {
                fprintf(stderr, "turntable: unknown --studio-set '%s' (expected pedestal or cyclo)\n",
                        studioSetString.c_str());
                return 1;
            }
            studioSets.push_back(studioSet);
        }
    }

    const std::vector<LightRig> rigs = _CollectLightRigs(args);
    if (rigs.empty()) {
        fprintf(stderr, "turntable: no light rigs available\n");
        return 1;
    }

    if (args.listLightRigs) {
        printf("Light rigs:\n");
        for (const LightRig &rig : rigs) {
            if (rig.isNoLight) {
                printf("  %s (no lights)\n", rig.name.c_str());
            } else if (rig.isTwoQuad) {
                printf("  %s (two rect lights)\n", rig.name.c_str());
            } else {
                printf("  %s (%s)\n", rig.name.c_str(), rig.hdri.c_str());
            }
        }
        return 0;
    }

    std::string selectedRigName;
    if (!args.hdri.empty()) {
        // --hdri builds a dedicated "custom_hdri" dome rig and forces its
        // selection, overriding whatever --light-rig requested.
        const std::string requestedRig = TfStringToLower(args.lightRig);
        if (requestedRig != "auto" && requestedRig != "custom_hdri") {
            printf("turntable: --hdri overrides --light-rig '%s'; using the custom HDRI rig\n",
                   args.lightRig.c_str());
        }
        selectedRigName = "custom_hdri";
    } else if (TfStringToLower(args.lightRig) == "auto") {
        selectedRigName = rigs.front().name;
    } else {
        const LightRig *selectedRig = _FindRigByName(rigs, args.lightRig);
        if (!selectedRig) {
            fprintf(stderr, "turntable: unknown --light-rig '%s'\n", args.lightRig.c_str());
            fprintf(stderr, "Available rigs:\n");
            for (const LightRig &rig : rigs) {
                fprintf(stderr, "  %s\n", rig.name.c_str());
            }
            return 1;
        }
        selectedRigName = selectedRig->name;
    }

    AssetBounds bounds;
    if (!ComputeAssetBounds(args.input, args.upAxis, bounds)) {
        return 1;
    }

    // Check for render settings in the asset.
    SdfPath   assetSettingsPath;
    GfVec2i   assetResolution(args.width, args.height);
    bool      hasAssetSettings = _FindAssetRenderSettings(args.input, assetSettingsPath, assetResolution);

    int effectiveWidth  = hasAssetSettings ? assetResolution[0] : args.width;
    int effectiveHeight = hasAssetSettings ? assetResolution[1] : args.height;

    // CreateNew fails if the file already exists.
    std::remove(args.output.c_str());
    UsdStageRefPtr stage = UsdStage::CreateNew(args.output);
    if (!stage) {
        fprintf(stderr, "turntable: cannot create output stage '%s'\n", args.output.c_str());
        return 1;
    }

    stage->SetStartTimeCode(0.0);
    stage->SetEndTimeCode(double(args.frames - 1));
    stage->SetFramesPerSecond(24.0);
    stage->SetTimeCodesPerSecond(24.0);
    UsdGeomSetStageUpAxis(stage, bounds.upAxis);

    _SetupAsset(stage, args.input);
    _SetupTurntableRoot(stage, mode, bounds, args.frames);
    if (!_SetupStudioSets(stage, studioSets, bounds, args)) {
        return 1;
    }
    const SdfPath cameraPath = _SetupCamera(stage, args, bounds, mode, effectiveWidth, effectiveHeight);
    _SetupLights(stage, args, bounds, mode, args.frames, rigs, selectedRigName);

    // Unless the no_light rig was chosen, deactivate the asset's own lights so the
    // turntable rig is the only illumination.
    const LightRig *chosenRig = _FindRigByName(rigs, selectedRigName);
    int deactivatedAssetLights = 0;
    if (chosenRig && !chosenRig->isNoLight) {
        deactivatedAssetLights = _DeactivateAssetLights(stage);
    }

    if (hasAssetSettings) {
        _OverrideAssetRenderSettings(stage, assetSettingsPath, cameraPath, args);
        stage->SetMetadata(UsdRenderTokens->renderSettingsPrimPath, assetSettingsPath.GetString());
    } else {
        _SetupRenderSettings(stage, args, cameraPath);
        stage->SetMetadata(UsdRenderTokens->renderSettingsPrimPath, std::string("/Render/TurntableSettings"));
    }

    stage->GetRootLayer()->Save();

    printf("Written: %s\n", args.output.c_str());
    printf("  Up axis   : %s\n", bounds.upAxis.GetText());
    printf("  BBox      : center (%.4g %.4g %.4g)  radius %.4g\n",
           bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
    printf("  Mode      : %s\n", _ModeToString(mode));
    printf("  Cam height: %.3g (rel), %.4g (world)\n",
           args.cameraHeight, _Lerp01(args.cameraHeight, bounds.upMin, bounds.upMax));
    printf("  Aim height: %.3g (rel), %.4g (world)\n",
           args.targetHeight, _Lerp01(args.targetHeight, bounds.upMin, bounds.upMax));
    printf("  Cam zoom  : %.3g\n", args.cameraZoom);
    if (!studioSets.empty()) {
        printf("  Studio Sets:");
        for (const StudioSetType studioSet : studioSets) {
            printf(" %s", _StudioSetToString(studioSet));
        }
        printf("\n");
    }
    printf("  Frames    : 0..%d\n", args.frames - 1);
    printf("  RenderSettings: %s (%s)\n",
           hasAssetSettings ? assetSettingsPath.GetText() : "/Render/TurntableSettings",
           hasAssetSettings ? "asset" : "generated");
    printf("  Resolution: %dx%d\n", effectiveWidth, effectiveHeight);
    printf("  Light rig : %s\n", selectedRigName.c_str());
    printf("  Light mult: %.3g\n", args.lightIntensity);
    printf("  Light rigs: %zu available\n", rigs.size());
    if (chosenRig && chosenRig->isNoLight) {
        printf("  Asset lights: kept (no_light rig)\n");
    } else {
        printf("  Asset lights: %d deactivated\n", deactivatedAssetLights);
    }
    if (args.backgroundColor.size() == 3) {
        printf("  Background : %.3g %.3g %.3g\n",
               args.backgroundColor[0], args.backgroundColor[1], args.backgroundColor[2]);
    }
    return 0;
}

} // namespace

int main(int argc, char const *argv[])
{
    CLI::App app(
        "turntable : Generate a USD turntable scene for lookdev.\n"
        "Creates a USD file that sublayers the asset, with an orbiting\n"
        "camera, selectable light rigs, and UsdRender settings ready for kick.\n",
        "turntable");

    Args args;
    Configure(&app, args);
    CLI11_PARSE(app, argc, argv);

    return Run(args);
}
