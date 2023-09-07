///
/// This is a simplifed usdGenSchema which to flatten the arnold schema file.
/// It is meant to be used with the arnold schema file only, don't use it as a usdGenSchema replacement. It flattens the
/// schema and creates a new plugInfo.json
///
/// It follows the python code written here:
///    https://github.com/PixarAnimationStudios/OpenUSD/blob/10b62439e9242a55101cf8b200f2c7e02420e1b0/pxr/usd/usd/usdGenSchema.py#L26
///

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include <pxr/base/arch/systemInfo.h> // ArchGetCwd
#include <pxr/base/js/json.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

template <typename... Args>
inline std::string _JoinPath(const std::string &head, Args... tail)
{
    return head + "/" + _JoinPath(tail...); // TODO: will the unix separator wor
}

template <>
inline std::string _JoinPath(const std::string &head)
{
    return head;
}

// Fills inherits
void _FindAllInherits(UsdPrim usdPrim, std::set<SdfPath> &inherits)
{
    UsdInherits usdInherits = usdPrim.GetInherits();
    SdfPathVector inheritsList = usdInherits.GetAllDirectInherits();
    for (SdfPath inheritPath : inheritsList) {
        inherits.insert(inheritPath);
        UsdPrim inheritedPrim = usdPrim.GetStage()->GetPrimAtPath(inheritPath);
        _FindAllInherits(inheritedPrim, inherits);
    }
}

SdfPrimSpecHandle _GetDefiningLayerAndPrim(UsdStageRefPtr stage, std::string schemaName)
{
    if (schemaName == "SchemaBase") {
        return {};
    } else {
        for (const auto &layer : stage->GetLayerStack()) {
            for (const auto &sdfPrim : layer->GetRootPrims()) {
                if (sdfPrim->GetName() == schemaName) {
                    return sdfPrim;
                }
            }
        }
    }
}

std::string _GetLibPrefix(SdfLayerHandle layer)
{
    auto globalPrim = layer->GetPrimAtPath(SdfPath("/GLOBAL"));
    auto customData = globalPrim->GetCustomData();
    auto libraryPrefixIt = customData.find("libraryPrefix");
    if (libraryPrefixIt != customData.end()) {
        return libraryPrefixIt->second.Get().Get<std::string>();
    } else {
        libraryPrefixIt = customData.find("libraryName");
        return libraryPrefixIt->second.Get().Get<std::string>();
    }
}

// Returns the given string (camelCase or ProperCase) in ProperCase,
// stripping out any non-alphanumeric characters.
void _ProperCase(std::string &aString)
{
    if (!aString.empty()) {
        auto endOfString =
            std::partition(aString.begin(), aString.end(), [](const auto &a) { return std::isalnum(a); });
        aString = aString.substr(0, std::distance(aString.begin(), endOfString));
        if (!aString.empty()) {
            aString[0] = std::toupper(aString[0]);
        }
    }
}

struct ClassInfo {
    ClassInfo(SdfPrimSpecHandle sdfPrim, UsdPrim usdPrim)
    {
        usdPrimTypeName = sdfPrim->GetPath().GetName();
        cppClassName = "Usd" + usdPrimTypeName;
        std::set<SdfPath> allInherits;
        _FindAllInherits(usdPrim, allInherits);
        const bool isTyped = allInherits.count(SdfPath("/Typed"));
        const bool isConcrete = sdfPrim->GetTypeName() != TfToken();
        const bool isTypedBase = cppClassName == "UsdTyped";
        const bool isAPISchemaBase = cppClassName == "UsdAPISchemaBase";
        auto customData = sdfPrim->GetCustomData();
        const bool isApi = !isTyped && !isConcrete && !isAPISchemaBase && !isTypedBase;
        auto apiSchemaTypeIt = customData.find("apiSchemaType");
        const auto apiSchemaType = apiSchemaTypeIt != customData.end()
                                       ? apiSchemaTypeIt->second.Get().Get<TfToken>().GetString()
                                       : (isApi ? "singleApply" : "");
        const bool isAppliedAPISchema = apiSchemaType == "singleApply" || apiSchemaType == "multipleApply";
        const bool isMultipleApply = apiSchemaType == "multipleAppy";
        if (isApi) {
            if (!isAppliedAPISchema) {
                schemaKind = "nonAppliedAPI";
            } else if (isMultipleApply) {
                schemaKind = "multipleApplyAPI";
            } else {
                schemaKind = "singleApplyAPI";
            }
        } else if (isTyped && !isTypedBase) {
            if (isConcrete) {
                schemaKind = "concreteTyped";
            } else {
                schemaKind = "abstractTyped";
            }
        } else {
            schemaKind = "abstractBase";
        }

        UsdInherits usdInherits = usdPrim.GetInherits();
        SdfPathVector inheritsList = usdInherits.GetAllDirectInherits();

        // Find parentPrim
        std::string parentClass = inheritsList.empty() ? std::string("SchemaBase") : inheritsList[0].GetName();
        // parentCppClassName +=  inheritsList.empty() ? std::string("SchemaBase") : inheritsList[0].GetName();
        SdfPrimSpecHandle parentPrim = _GetDefiningLayerAndPrim(usdPrim.GetStage(), parentClass);

        if (parentPrim) {
            parentCppClassName = _GetLibPrefix(parentPrim->GetLayer());
            _ProperCase(parentCppClassName);
            auto parentPrimCustomData = parentPrim->GetCustomData();
            auto parentClassNameIt = parentPrimCustomData.find("className");
            parentCppClassName += parentClassNameIt != parentPrimCustomData.end()
                                      ? parentClassNameIt->second.Get().Get<TfToken>().GetString()
                                      : parentClass;
        }

        // The python code makes sure there is only 1 or 0 inherit
        // auto inherits = usdPrim.GetMetadata(TfToken("inheritPaths"));

        // Save the extraPlugInfo for later use
        auto extraPlugInfoIt = customData.find("extraPlugInfo");
        if (extraPlugInfoIt != customData.end()) {
            extraPlugInfo = extraPlugInfoIt->second.Get();
        }
    }

    std::string usdPrimTypeName;
    std::string cppClassName;
    std::string parentCppClassName;
    std::string schemaKind;
    VtValue extraPlugInfo;
};

inline TfToken mangle(const TfToken &name)
{
    const std::string prefix("__MANGLED_TO_AVOID_BUILTINS__");
    return TfToken(prefix + name.GetString());
}

inline TfToken demangle(const TfToken &name)
{
    constexpr size_t prefixSize = 29; // = std::char_traits<char>::length("__MANGLED_TO_AVOID_BUILTINS__"); // C++17
    return TfToken(name.GetString().substr(prefixSize));
}

VtDictionary _GetLibMetadata(SdfLayerRefPtr layer)
{
    SdfPrimSpecHandle globalPrim = layer->GetPrimAtPath(SdfPath("/GLOBAL"));
    return globalPrim ? globalPrim->GetCustomData() : VtDictionary{};
}

bool _UseLiteralIdentifierForLayer(SdfLayerRefPtr layer)
{
    const VtValue *val = _GetLibMetadata(layer).GetValueAtPath("useLiteralIdentifier");
    if (val) {
        return val->Get<bool>();
    } else {
        return true;
    }
}

// The original python ParseUsd function does also run some checks on the prims.
// We don't run those checks as usdGenSchema does it already and here we just want to flatten our
// already tested schema
void ParseUsd(const std::string &usdFilePath, std::vector<ClassInfo> &classes)
{
    auto sdfLayer = SdfLayer::FindOrOpen(usdFilePath);
    auto stage = UsdStage::Open(sdfLayer);

    bool hasInvalidFields = false;
    bool useLiteralIdentifier = _UseLiteralIdentifierForLayer(sdfLayer);

    // PARSE CLASSES
    for (const auto &sdfPrim : sdfLayer->GetRootPrims()) {
        if (sdfPrim->GetSpecifier() != SdfSpecifier::SdfSpecifierClass) {
            continue;
        }
        // Tests field validation
        // if not _ValidateFields(sdfPrim):
        //    hasInvalidFields = True

        UsdPrim usdPrim = stage->GetPrimAtPath(sdfPrim->GetPath());
        // classInfo = ClassInfo(usdPrim, sdfPrim, useLiteralIdentifier)
        // ClassInfo classInfo(sdfPrim, usdPrim);
        //  In the python code, there are some tests to make sure the prims are valid.
        //  if classInfo.apiSchemaType == MULTIPLE_APPLY:
        //      // do tests
        classes.emplace_back(sdfPrim, usdPrim);
    }
    // The original code runs a set of checks in this loop, but we don't need them with our schema
    // for (const auto &classInfo:classes) {
    //
    // }
}

void _WriteDictionaryContent(const VtDictionary &dict, JsWriter &writer)
{
    // Iterate on dictionary items
    for (const auto &keyValue : dict) {
        const std::string &key = keyValue.first;
        const VtValue &value = keyValue.second;
        if (value.IsHolding<bool>()) {
            writer.WriteKeyValue(key, value.Get<bool>());
        }
    }
}

void _WriteExtraPlugInfo(const ClassInfo &cls, JsWriter &writer)
{
    if (cls.extraPlugInfo.IsHolding<VtDictionary>()) {
        const VtDictionary &dict = cls.extraPlugInfo.Get<VtDictionary>();
        _WriteDictionaryContent(dict, writer);
    }
}

void _CreateJsonClasses(const std::vector<ClassInfo> &classes, JsWriter &writer)
{
    for (const auto &cls : classes) {
        // clang-format off
        writer.WriteKey(cls.cppClassName);
        writer.BeginObject();
            writer.WriteKey("alias");
            writer.BeginObject();
                writer.WriteKeyValue("UsdSchemaBase", cls.usdPrimTypeName);
            writer.EndObject();
            writer.WriteKeyValue("autoGenerated", true);
            writer.WriteKey("bases");
            writer.BeginArray();
                writer.WriteValue(cls.parentCppClassName);
            writer.EndArray();
            _WriteExtraPlugInfo(cls, writer);
            writer.WriteKeyValue("schemaKind", cls.schemaKind);
        writer.EndObject();
        // clang-format on
    }
}

void GeneratePlugInfo(
    const std::string &codeGenPath, const std::string &filePath, const std::vector<ClassInfo> &classes,
    bool validate /*arg.validate*/ /*, env*/)
{
    std::ofstream plugInfoDst(_JoinPath(codeGenPath, "plugInfo.json"));
    JsWriter writer(plugInfoDst, JsWriter::Style::Pretty);
    // clang-format off
    writer.BeginObject();
        writer.WriteKey("Plugins");
        writer.BeginArray();
            writer.BeginObject();
                writer.WriteKey("Info");
                writer.BeginObject();
                    writer.WriteKey("Types");
                    writer.BeginObject();
                        _CreateJsonClasses(classes, writer);
                    writer.EndObject();
                writer.EndObject();
                writer.WriteKeyValue("Name", "usdArnold");
                writer.WriteKeyValue("ResourcePath", "resources");
                writer.WriteKeyValue("Root", "..");
                // Note that if any explicit cpp code is included for this schema
                // domain, the plugin 'Type' needs to be manually updated in the
                // generated plugInfo.json to "library".
                writer.WriteKeyValue("Type", "resource"); // skipCodegen
            writer.EndObject();
        writer.EndArray();
    writer.EndObject();
    // clang-format on
}

SdfLayerRefPtr _MakeFlattenedRegistryLayer(const std::string &filePath)
{
    auto stage = UsdStage::Open(filePath);
    {
        UsdEditContext editContext(stage, UsdEditTarget(stage->GetSessionLayer()));
        // Mangle names in edit context
        for (auto cls : stage->GetPseudoRoot().GetAllChildren()) {
            if (cls.GetTypeName() != TfToken()) {
                cls.SetTypeName(mangle(cls.GetTypeName()));
            }
        }
    }
    SdfLayerRefPtr flatLayer = stage->Flatten(false);

    // demangle
    for (const auto &cls : flatLayer->GetRootPrims()) {
        if (cls->GetTypeName() != TfToken()) {
            cls->SetTypeName(demangle(cls->GetTypeName()));
        }
    }

    // In order to prevent derived classes from inheriting base class
    // documentation metadata, we must manually replace docs here.
    for (const SdfLayerHandle &layer : stage->GetLayerStack()) {
        for (const auto &cls : layer->GetRootPrims()) {
            auto flatCls = flatLayer->GetPrimAtPath(cls->GetPath());
            if (cls->HasInfo(TfToken("documentation"))) {
                flatCls->SetInfo(TfToken("documentation"), cls->GetInfo(TfToken("documentation")));
            } else {
                flatCls->ClearInfo(TfToken("documentation"));
            }
        }
    }
    return flatLayer;
}

void _RenamePropertiesWithInstanceablePrefix(UsdPrim &usdPrim)
{
    auto originalPropNames = usdPrim.GetPropertyNames();
    if (originalPropNames.empty()) {
        return;
    }
    auto namespacePrefix = usdPrim.GetCustomDataByKey(TfToken("propertyNamespacePrefix")).GetWithDefault(std::string());
    if (namespacePrefix.empty()) {
        // The original python code return errors here
        return;
    }
    for (auto prop : usdPrim.GetProperties()) {
        auto newPropName = UsdSchemaRegistry::MakeMultipleApplyNameTemplate(namespacePrefix, prop.GetName());
        prop.FlattenTo(usdPrim, newPropName);
    }
    for (auto name : originalPropNames) {
        usdPrim.RemoveProperty(name);
    }
}

void GenerateRegistry(
    const std::string &codeGenPath, const std::string &filePath, const std::vector<ClassInfo> &classes,
    bool validate /*arg.validate*/ /*, env*/)
{
    SdfLayerRefPtr flatLayer = _MakeFlattenedRegistryLayer(filePath);

    auto flatStage = UsdStage::Open(flatLayer);
    SdfPathVector pathsToDelete;
    std::unordered_map<std::string, ClassInfo> primsToKeep;
    for (const auto &classInfo : classes) {
        primsToKeep.emplace(classInfo.usdPrimTypeName, classInfo);
    }

    if (!flatStage->RemovePrim(SdfPath("/GLOBAL"))) {
        std::cerr << "ERROR: Could not remove GLOBAL prim." << std::endl;
    }
    // SHould be this one ???  std::unordered_map<TfToken, VtArray<TfToken>, TfToken::HashFunctor>
    // allFallbackSchemaPrimTypes;
    std::unordered_map<TfToken, TfTokenVector, TfToken::HashFunctor> allFallbackSchemaPrimTypes;

    for (auto p : flatStage->GetPseudoRoot().GetAllChildren()) {
        if (primsToKeep.find(p.GetName()) == primsToKeep.end()) {
            pathsToDelete.push_back(p.GetPath());
            continue;
        }

        // > 22.11
        // auto familyAndVersion = UsdSchemaRegistry::ParseSchemaFamilyAndVersionFromIdentifier(TfToken(p.GetName()));
        // auto family = familyAndVersion.first;

        auto family = p.GetName(); // 22.11
        if (TfStringEndsWith(family.GetString(), "API")) {
            // apiSchemaType = p.GetCustomDataByKey(API_SCHEMA_TYPE) or SINGLE_APPLY // TODO: use global TfToken ?
            auto apiSchemaType =
                p.GetCustomDataByKey(TfToken("apiSchemaType")).GetWithDefault(std::string("singleApply"));
            if (apiSchemaType == "multipleApply") {
                _RenamePropertiesWithInstanceablePrefix(p);
            }
            std::vector<std::string> allowedAPIMetadata = {"specifier", "customData", "documentation"};
            if (apiSchemaType == "singleApply" || apiSchemaType == "multipleApply") {
                allowedAPIMetadata.push_back("apiSchemas");
            }
            // We should test for invalidData here, but we expect only valid data in our schema
            // invalidMetadata = [key for key in p.GetAllAuthoredMetadata().keys()
            //            if key not in allowedAPIMetadata]
        }
        if (p.HasAuthoredTypeName()) {
            auto fallbackTypes = p.GetCustomDataByKey(TfToken("fallbackTypes"))
                                     .GetWithDefault(TfTokenVector{}); // TODO what type is that
            if (!fallbackTypes.empty()) {
                allFallbackSchemaPrimTypes.emplace(p.GetName(), fallbackTypes);
            }
        }
        // Original python code adds:
        // # Set the full list of the class's applied API apiSchemas as an explicit
        // # list op in the apiSchemas metadata. Note that this API schemas list
        // # will have been converted to template names if the class is a multiple
        // # apply API schema.
        // appliedAPISchemas = primsToKeep[p.GetName()].allAppliedAPISchemas
        // if appliedAPISchemas:
        //     p.SetMetadata('apiSchemas', Sdf.TokenListOp.CreateExplicit(appliedAPISchemas))

        p.ClearCustomData();

        for (auto myproperty : p.GetAuthoredProperties()) {
            myproperty.ClearCustomData();
        }

        // Original python code adds:
        // apiSchemaOverridePropertyNames = sorted(
        //     primsToKeep[p.GetName()].apiSchemaOverridePropertyNames)
        // if apiSchemaOverridePropertyNames:
        //     p.SetCustomDataByKey('apiSchemaOverridePropertyNames',
        //                          Vt.TokenArray(apiSchemaOverridePropertyNames))
    }

    for (auto p : pathsToDelete) {
        flatStage->RemovePrim(p);
    }

    flatLayer->SetComment("WARNING: THIS FILE IS GENERATED BY usdGenSchemaArnold. DO NOT EDIT.");

    // Remove doxygen tags from schema registry docs.
    // ExportToString escapes '\' again, so take that into account.
    std::string layerSource;
    flatLayer->ExportToString(&layerSource);
    layerSource = TfStringReplace(layerSource, "\\\\em ", "");
    layerSource = TfStringReplace(layerSource, "\\\\li", "-");
    // Pray for the regex_replace to work on all platform
    layerSource = std::regex_replace(layerSource, std::regex("\\\\+ref [^\\s]+ "), "");
    layerSource = std::regex_replace(layerSource, std::regex("\\\\+section [^\\s]+ "), "");

    std::ofstream generatedSchema;
    generatedSchema.open(_JoinPath(codeGenPath, "generatedSchema.usda"));
    generatedSchema << layerSource;
    generatedSchema.close();
}

void InitializeResolver()
{
    ArSetPreferredResolver("ArDefaultResolver");
    PlugRegistry &pr = PlugRegistry::GetInstance();
    std::vector<std::string> resourcePaths;
    std::set<TfType> derivedTypes;
    PlugRegistry::GetAllDerivedTypes(PlugRegistry::FindTypeByName("UsdSchemaBase"), &derivedTypes);

    for (const auto &t : derivedTypes) {
        auto plugin = pr.GetPluginForType(t);
        if (plugin) {
            resourcePaths.push_back(plugin->GetResourcePath());
        }
    }
    std::sort(resourcePaths.begin(), resourcePaths.end());
    ArDefaultResolver::SetDefaultSearchPath(resourcePaths);
}

int main(int argc, const char **argv)
{
    if (argc < 3) {
        std::cerr << "ERRROR: invalid number of command line arguments" << std::endl;
        return 1;
    }
    std::string srcFile(argv[1]);
    std::string dstDir(argv[2]);

    InitializeResolver();

    std::vector<ClassInfo> classes;
    ParseUsd(srcFile, classes);
    std::sort(classes.begin(), classes.end(), [](const auto &a, const auto &b) {
        return a.usdPrimTypeName < b.usdPrimTypeName;
    });
    GenerateRegistry(dstDir, srcFile, classes, false);
    GeneratePlugInfo(dstDir, srcFile, classes, false);
    return 0;
}
