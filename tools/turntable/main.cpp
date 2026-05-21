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
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
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
    bool        listLightRigs = false;
    std::string upAxis;   // empty = auto-detect from stage
    double      cameraHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      targetHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      cameraZoom   = 1.0; // Multiplicative factor for camera orbit distance.
    std::string mode         = "camera"; // camera (default), object, or light
    std::string props;  // Comma-separated prop names
};

struct LightRig {
    std::string name;
    std::string hdri;
    bool        isTwoQuad = false;
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

enum class PropType {
    Ground,
    Cyclo
};

bool _ParseProp(const std::string &propString, PropType &out)
{
    const std::string prop = TfStringToLower(propString);
    if (prop == "pedestral") {
        out = PropType::Ground;
        return true;
    }
    if (prop == "cyclo" || prop == "cyc" || prop == "cyclorama") {
        out = PropType::Cyclo;
        return true;
    }
    return false;
}

const char *_PropToString(PropType prop)
{
    switch (prop) {
    case PropType::Ground: return "pedestral";
    case PropType::Cyclo: return "cyclo";
    }
    return "pedestral";
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
        "Number of frames for a full 360 rotation (default: 96)")
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
        "Directory scanned for .exr environment maps to generate dome light rigs")
        ->option_text("DIR");

    app->add_option("--light-rig", args.lightRig,
        "Light rig to select: auto, one discovered HDRI rig, or two_quad")
        ->option_text("NAME");

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

    app->add_option("--prop", args.props,
        "Add props to the generated stage. Supported values: pedestral, cyclo (comma-separated)")
        ->option_text("NAME");

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

    const std::string envDirs = ArchGetEnv("ARNOLD_TURNABLE_HDRI_DIRS");
    if (!envDirs.empty()) {
        const std::vector<std::string> splitDirs = TfStringSplit(envDirs, ARCH_PATH_LIST_SEP);
        for (const std::string &directory : splitDirs) {
            addDirectory(directory);
        }
    }

    return directories;
}

std::vector<std::string> _ListExrFiles(const std::string &directory)
{
    std::vector<std::string> files;

#ifdef _WIN32
    const std::string pattern = TfStringCatPaths(directory, "*.exr");
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
        if (TfStringGetSuffix(TfStringToLower(fileName)) == "exr") {
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
        if (TfStringGetSuffix(TfStringToLower(fileName)) == "exr") {
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

            const std::vector<std::string> files = _ListExrFiles(hdriDir);
            for (const std::string &fileName : files) {
                const std::string baseName = TfStringGetBeforeSuffix(fileName);
                std::string rigName = _SanitizeRigName(baseName);
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

    const GfBBox3d worldBBox = bboxCache.ComputeWorldBound(stage->GetPseudoRoot());
    const GfRange3d range    = worldBBox.ComputeAlignedRange();

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
    const SdfPath materialPath("/materials/pedestral");
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

    UsdGeomMesh cyclo = UsdGeomMesh::Define(stage, SdfPath("/props/cyclo"));
    cyclo.CreatePointsAttr().Set(points);
    cyclo.CreateFaceVertexCountsAttr().Set(faceVertexCounts);
    cyclo.CreateFaceVertexIndicesAttr().Set(faceVertexIndices);
    cyclo.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    cyclo.CreateDoubleSidedAttr().Set(true);
    cyclo.CreateDisplayColorAttr().Set(VtArray<GfVec3f>{GfVec3f(0.18f, 0.18f, 0.18f)});
    UsdShadeMaterialBindingAPI::Apply(cyclo.GetPrim()).Bind(material);
    return true;
}

static bool _SetupProps(const UsdStageRefPtr &stage, const std::vector<PropType> &props,
                        const AssetBounds &bounds, const Args &args)
{
    UsdGeomXform sceneProps = UsdGeomXform::Define(stage, SdfPath("/props"));
    (void)sceneProps;
    const UsdShadeMaterial groundMaterial = _CreateGroundMaterial(stage);
    const std::set<PropType> activeProps(props.begin(), props.end());

    const auto _IsPropActive = [&activeProps](PropType prop) {
        return activeProps.find(prop) != activeProps.end();
    };

    {
        const double thickness = std::max(bounds.bottomFaceDiagonal * 0.01, 0.001);
        const double gap = std::max(bounds.bottomFaceDiagonal * 0.001, 0.0001);
        const double groundHeight = bounds.upMin - gap - (thickness * 0.5);

        UsdGeomCylinder ground = UsdGeomCylinder::Define(stage, SdfPath("/props/pedestral"));
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

        ground.GetPrim().SetActive(_IsPropActive(PropType::Ground));
    }

    if (!_CreateCyclo(stage, bounds, args, groundMaterial)) {
        return false;
    }
    stage->GetPrimAtPath(SdfPath("/props/cyclo")).SetActive(_IsPropActive(PropType::Cyclo));

    return true;
}

// /asset  —  the input asset referenced in
static void _SetupAsset(const UsdStageRefPtr &stage, const std::string &assetPath,
                        TurntableMode mode, const AssetBounds &bounds, int frames)
{
    UsdGeomXform asset = UsdGeomXform::Define(stage, SdfPath("/asset"));
    asset.GetPrim().GetReferences().AddReference(assetPath);

    if (mode != TurntableMode::RotateObject) {
        return;
    }

    UsdGeomXformOp matOp = asset.AddTransformOp();
    for (int i = 0; i < frames; ++i) {
        const double angleDegrees = 360.0 * double(i) / double(frames);
        matOp.Set(_RotationAroundPivot(bounds.upAxis, angleDegrees, bounds.center), UsdTimeCode(double(i)));
    }
}

// /camera  —  camera with a time-sampled look-at matrix that orbits the asset center
static SdfPath _SetupCamera(const UsdStageRefPtr &stage, const Args &args,
                            const AssetBounds &bounds, TurntableMode mode)
{
    const SdfPath cameraPath("/camera");
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
    const float vaperture = haperture * float(args.height) / float(args.width);
    cam.GetHorizontalApertureAttr().Set(haperture);
    cam.GetVerticalApertureAttr().Set(vaperture);
    cam.GetFocalLengthAttr().Set(35.0f);
    cam.GetClippingRangeAttr().Set(GfVec2f(float(nearClip), float(farClip)));

    return cameraPath;
}

// /lights/dome  —  dome light, optionally driven by an HDRI texture or color
static void _SetupDomeLight(const UsdStageRefPtr &stage, const SdfPath &lightPath,
                            const std::string &hdri, const GfVec3f &color = GfVec3f(1.0f))
{
    UsdLuxDomeLight dome = UsdLuxDomeLight::Define(stage, lightPath);
    UsdLuxLightAPI api = UsdLuxLightAPI::Apply(dome.GetPrim());
    api.CreateIntensityAttr().Set(1.0f);
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
    const float leftIntensity = _ComputeQuadLightIntensity((target - leftPos).GetLength(), width, height);
    const float rightIntensity = _ComputeQuadLightIntensity((target - rightPos).GetLength(), width, height);

    UsdLuxRectLight left = UsdLuxRectLight::Define(stage, SdfPath("/lights/key_left"));
    UsdLuxLightAPI leftApi = UsdLuxLightAPI::Apply(left.GetPrim());
    leftApi.CreateIntensityAttr().Set(leftIntensity);
    leftApi.CreateSpecularAttr().Set(0.0f);
    left.CreateWidthAttr().Set(static_cast<float>(width));
    left.CreateHeightAttr().Set(static_cast<float>(height));
    _SetLookAtTransform(UsdGeomXformable(left.GetPrim()), leftPos, target, up);

    UsdLuxRectLight rightLight = UsdLuxRectLight::Define(stage, SdfPath("/lights/key_right"));
    UsdLuxLightAPI rightApi = UsdLuxLightAPI::Apply(rightLight.GetPrim());
    rightApi.CreateIntensityAttr().Set(rightIntensity);
    rightApi.CreateSpecularAttr().Set(0.0f);
    rightLight.CreateWidthAttr().Set(static_cast<float>(width));
    rightLight.CreateHeightAttr().Set(static_cast<float>(height));
    _SetLookAtTransform(UsdGeomXformable(rightLight.GetPrim()), rightPos, target, up);
}

static void _SetupLights(const UsdStageRefPtr &stage, const Args &args,
                         const AssetBounds &bounds, TurntableMode mode, int frames,
                         const std::vector<LightRig> &rigs, const std::string &selectedRig)
{
    UsdGeomXform lights = UsdGeomXform::Define(stage, SdfPath("/lights"));

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

        if (rig.isTwoQuad) {
            _SetupTwoQuadRig(stage, bounds, args);
        } else {
            // Use white color for white_dome rig, otherwise no color override
            if (rig.name == "white_dome") {
                _SetupDomeLight(stage, SdfPath("/lights/dome"), rig.hdri, GfVec3f(1.0f, 1.0f, 1.0f));
            } else {
                _SetupDomeLight(stage, SdfPath("/lights/dome"), rig.hdri);
            }
        }
    }

    rigVariantSet.SetVariantSelection(selectedRig);
}

// /Render  —  UsdRenderSettings + product + beauty AOV
static void _SetupRenderSettings(const UsdStageRefPtr &stage, const Args &args,
                                  const SdfPath &cameraPath)
{
    UsdRenderSettings settings = UsdRenderSettings::Define(stage, SdfPath("/Render/settings"));
    settings.GetResolutionAttr().Set(GfVec2i(args.width, args.height));
    settings.GetCameraRel().SetTargets({cameraPath});

    // Output image sequence: strip the .usda extension and append frame pattern.
    const std::string productPath = TfStringGetBeforeSuffix(args.output) + ".####.exr";
    UsdRenderProduct product = UsdRenderProduct::Define(stage, SdfPath("/Render/product"));
    product.GetProductNameAttr().Set(SdfAssetPath(productPath));
    product.GetProductTypeAttr().Set(TfToken("arnold"));
    settings.GetProductsRel().AddTarget(SdfPath("/Render/product"));

    // Beauty AOV (RGBA).
    UsdRenderVar beauty = UsdRenderVar::Define(stage, SdfPath("/Render/vars/beauty"));
    beauty.GetDataTypeAttr().Set(TfToken("color4f"));
    beauty.GetSourceNameAttr().Set(std::string("RGBA"));
    beauty.GetSourceTypeAttr().Set(UsdRenderTokens->raw);
    product.GetOrderedVarsRel().AddTarget(SdfPath("/Render/vars/beauty"));
}

int Run(const Args &args)
{
    if (args.input.empty() && !args.listLightRigs) {
        fprintf(stderr, "turntable: input is required unless --list-light-rigs is used\n");
        return 1;
    }

    if (args.cameraZoom <= 0.0) {
        fprintf(stderr, "turntable: --camera-zoom must be > 0 (got %.4g)\n", args.cameraZoom);
        return 1;
    }

    TurntableMode mode;
    if (!_ParseMode(args.mode, mode)) {
        fprintf(stderr, "turntable: unknown --mode '%s' (expected camera, object, or light)\n",
                args.mode.c_str());
        return 1;
    }

    std::vector<PropType> props;
    if (!args.props.empty()) {
        // Split comma-separated prop names
        std::vector<std::string> propNames;
        std::string current;
        for (char c : args.props) {
            if (c == ',') {
                if (!current.empty()) {
                    propNames.push_back(current);
                    current.clear();
                }
            } else if (c != ' ') {  // Skip whitespace
                current += c;
            }
        }
        if (!current.empty()) {
            propNames.push_back(current);
        }
        
        props.reserve(propNames.size());
        for (const std::string &propString : propNames) {
            PropType prop;
            if (!_ParseProp(propString, prop)) {
                fprintf(stderr, "turntable: unknown --prop '%s' (expected pedestral or cyclo)\n",
                        propString.c_str());
                return 1;
            }
            props.push_back(prop);
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
            if (rig.isTwoQuad) {
                printf("  %s (two rect lights)\n", rig.name.c_str());
            } else {
                printf("  %s (%s)\n", rig.name.c_str(), rig.hdri.c_str());
            }
        }
        return 0;
    }

    std::string selectedRigName;
    if (TfStringToLower(args.lightRig) == "auto") {
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

    _SetupAsset(stage, args.input, mode, bounds, args.frames);
    if (!_SetupProps(stage, props, bounds, args)) {
        return 1;
    }
    const SdfPath cameraPath = _SetupCamera(stage, args, bounds, mode);
    _SetupLights(stage, args, bounds, mode, args.frames, rigs, selectedRigName);
    _SetupRenderSettings(stage, args, cameraPath);

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
    if (!props.empty()) {
        printf("  Props     :");
        for (const PropType prop : props) {
            printf(" %s", _PropToString(prop));
        }
        printf("\n");
    }
    printf("  Frames    : 0..%d\n", args.frames - 1);
    printf("  Resolution: %dx%d\n", args.width, args.height);
    printf("  Light rig : %s\n", selectedRigName.c_str());
    printf("  Light rigs: %zu available\n", rigs.size());
    return 0;
}

} // namespace

int main(int argc, char const *argv[])
{
    CLI::App app(
        "turntable : Generate a USD turntable scene for lookdev.\n"
        "Creates a USD file with the asset as a reference, an orbiting\n"
        "camera, selectable light rigs, and UsdRender settings ready for kick.\n",
        "turntable");

    Args args;
    Configure(&app, args);
    CLI11_PARSE(app, argc, argv);

    return Run(args);
}
