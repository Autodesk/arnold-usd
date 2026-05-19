// SPDX-License-Identifier: Apache-2.0
#include <pxr/pxr.h>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/pxrCLI11/CLI11.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/tokens.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdRender/var.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_CLI;

namespace {

static const double _kPi = 3.14159265358979323846;

struct Args {
    std::string input;
    std::string output    = "turntable.usda";
    int         frames    = 96;
    int         width     = 1280;
    int         height    = 720;
    std::string hdri;
    std::string upAxis;   // empty = auto-detect from stage
    double      cameraHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      targetHeight = 0.5; // Relative to bbox height: 0 = bottom, 1 = top.
    double      cameraZoom   = 1.0; // Multiplicative factor for camera orbit distance.
    std::string mode         = "camera"; // camera (default), object, or light
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
    app->add_option("input", args.input, "USD asset file to turntable")
        ->required()
        ->option_text("FILE");

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
        "Path to an HDRI texture for the dome light")
        ->option_text("FILE");

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
}

struct AssetBounds {
    GfVec3d center;
    GfVec3d size;
    double  radius;  // half diagonal of the aligned bounding box
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
    out.upMin = range.GetMin()[upAxisIndex];
    out.upMax = range.GetMax()[upAxisIndex];
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

        // Orbit in the plane perpendicular to the up axis.
        GfVec3d eye;
        GfVec3d target;
        if (bounds.upAxis == UsdGeomTokens->z) {
            eye = GfVec3d(bounds.center[0] + camDistance * std::cos(angle),
                          bounds.center[1] + camDistance * std::sin(angle),
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

// /lights/dome  —  dome light, optionally driven by an HDRI texture
static void _SetupDomeLight(const UsdStageRefPtr &stage, const std::string &hdri,
                            TurntableMode mode, const AssetBounds &bounds, int frames)
{
    UsdGeomXform lights = UsdGeomXform::Define(stage, SdfPath("/lights"));
    UsdLuxDomeLight dome = UsdLuxDomeLight::Define(stage, SdfPath("/lights/dome"));
    UsdLuxLightAPI::Apply(dome.GetPrim()).CreateIntensityAttr().Set(1.0f);
    if (!hdri.empty()) {
        dome.CreateTextureFileAttr().Set(SdfAssetPath(hdri));
        dome.CreateTextureFormatAttr().Set(UsdLuxTokens->latlong);
    }

    if (mode != TurntableMode::RotateLight) {
        return;
    }

    UsdGeomXformOp matOp = lights.AddTransformOp();
    for (int i = 0; i < frames; ++i) {
        const double angleDegrees = 360.0 * double(i) / double(frames);
        matOp.Set(_RotationMatrixForUpAxis(bounds.upAxis, angleDegrees), UsdTimeCode(double(i)));
    }
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
    settings.GetProductsRel().AddTarget(SdfPath("/Render/product"));

    // Beauty AOV (Ci — full spectral beauty).
    UsdRenderVar beauty = UsdRenderVar::Define(stage, SdfPath("/Render/vars/beauty"));
    beauty.GetDataTypeAttr().Set(TfToken("color3f"));
    beauty.GetSourceNameAttr().Set(std::string("Ci"));
    beauty.GetSourceTypeAttr().Set(UsdRenderTokens->raw);
    product.GetOrderedVarsRel().AddTarget(SdfPath("/Render/vars/beauty"));
}

int Run(const Args &args)
{
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
    const SdfPath cameraPath = _SetupCamera(stage, args, bounds, mode);
    _SetupDomeLight(stage, args.hdri, mode, bounds, args.frames);
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
    printf("  Frames    : 0..%d\n", args.frames - 1);
    printf("  Resolution: %dx%d\n", args.width, args.height);
    if (!args.hdri.empty()) {
        printf("  HDRI      : %s\n", args.hdri.c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char const *argv[])
{
    CLI::App app(
        "turntable : Generate a USD turntable scene for lookdev.\n"
        "Creates a USD file with the asset as a reference, an orbiting\n"
        "camera, a dome light, and UsdRender settings ready for kick.\n",
        "turntable");

    Args args;
    Configure(&app, args);
    CLI11_PARSE(app, argc, argv);

    return Run(args);
}
