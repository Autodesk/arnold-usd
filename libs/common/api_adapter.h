//
// SPDX-License-Identifier: Apache-2.0
//


#pragma once

#include <pxr/usd/usdGeom/primvar.h>
#include "constant_strings.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

// Hash function for std::pair<std::string, TfToken>
struct PairStringTfTokenHash {
    std::size_t operator()(const std::pair<std::string, TfToken>& p) const {
        std::size_t h1 = std::hash<std::string>{}(p.first);
        std::size_t h2 = p.second.Hash();
        // Combine hashes using boost's hash_combine algorithm
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

// This is a base class used to call Arnold API functions within a particular context.
// For example we might want to wrap the AiNode call and add mutex, or store the nodes dependending on the context.
class ArnoldAPIAdapter {
public:

    ArnoldAPIAdapter() {}
    virtual ~ArnoldAPIAdapter() {}
    
    // Type of connection between 2 nodes
    // CONNECTION_LINK is for shader graph evaluation, 
    // CONNECTION_PTR is for simple node references, 
    // and CONNECTION_ARRAY is for multiple node references.
    enum ConnectionType {
        CONNECTION_LINK = 0,
        CONNECTION_PTR = 1,
        CONNECTION_ARRAY
    };
    struct Connection {
            AtNode *sourceNode;
            std::string sourceAttr;
            std::string target;
            ConnectionType type;
            std::string outputElement;
    };

    virtual AtNode *CreateArnoldNode(const char *type, const char *name) = 0;

    virtual void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        ConnectionType type, const std::string &outputElement = std::string()) 
    {
        std::lock_guard<AtMutex> lock(_connectionMutex);
        _connections.push_back(Connection());
        Connection &conn = _connections.back();
        conn.sourceNode = source;
        conn.sourceAttr = attr;
        conn.target = target;
        conn.type = type;
        conn.outputElement = outputElement;
    }
    virtual void AddNodeName(const std::string &name, AtNode *node) = 0;
    virtual AtNode* LookupTargetNode(const char *targetName, const AtNode* source, ConnectionType c) = 0;


    virtual const AtString& GetPxrMtlxPath() = 0;

    virtual void ProcessConnections()
    {
        std::lock_guard<AtMutex> lock(_connectionMutex);
        for (const auto& connection : _connections)
            ProcessConnection(connection);
        
        ClearConnections();
    }
    const std::vector<Connection>& GetConnections() const {return _connections;}
    void ClearConnections() {_connections.clear();}

    // Add a connection alias. This function is used when a new arnold node is created and it's name doesn't correspond
    // to the usd prim name. In that case we store the mapping from the usd prim it was created to the new arnold name
    void AddConnectionPathAlias(const std::string &usdPath, TfToken terminalName, const std::string &arnoldPath)
    {
        if (usdPath != arnoldPath) {
            // Remove :i1 :i2, any added suffix from the terminal name
            auto names = TfStringTokenize(terminalName.GetString(), ":");
            if (names.size() > 1) {
                terminalName = TfToken(names[0]); // Remove any namespaces
            }
            auto it = _connectionPathsAliases.find(std::make_pair(usdPath, terminalName));
            if (it == _connectionPathsAliases.end()) {
                _connectionPathsAliases[std::make_pair(usdPath, terminalName)] = arnoldPath;
            } else {
                // we concatenate the paths found and separate them with the space character.
                it->second = it->second + " " + arnoldPath;
            }
        }
    }

    virtual bool ProcessConnection(const Connection& connection)
    {
        if (connection.type == ArnoldAPIAdapter::CONNECTION_ARRAY) {
            std::vector<AtNode *> vecNodes;
            std::vector<std::string> targetPaths = TfStringTokenize(connection.target);
            for (const auto &targetPath : targetPaths) {
                _LookupTargetNodeArrayWithAlias(vecNodes, targetPath.c_str(), connection.sourceNode, connection.type, connection.sourceAttr);
            }
            AiNodeSetArray(
                connection.sourceNode, AtString(connection.sourceAttr.c_str()),
                AiArrayConvert(vecNodes.size(), 1, AI_TYPE_NODE, &vecNodes[0]));
        } else {
            AtNode *target = _LookupTargetNodeWithAlias(connection.target.c_str(), connection.sourceNode, connection.type, connection.sourceAttr);
            if (target == nullptr)
                return false;// node is missing, we don't process the connection

            if (connection.type == ArnoldAPIAdapter::CONNECTION_PTR) {
                if (connection.sourceAttr.back() == ']' ) {
                    std::stringstream ss(connection.sourceAttr);
                    std::string arrayAttr, arrayIndexStr;
                    if (std::getline(ss, arrayAttr, '[') && std::getline(ss, arrayIndexStr, ']')) {
                        int arrayIndex = std::stoi(arrayIndexStr);
                        AtArray *array = AiNodeGetArray(connection.sourceNode,
                                                AtString(arrayAttr.c_str()));
                        if (array == nullptr) {
                            array = AiArrayAllocate(arrayIndex + 1, 1, AI_TYPE_POINTER);
                            for (unsigned i=0; i<(unsigned) arrayIndex; i++)
                                AiArraySetPtr(array, i, nullptr);
                            AiArraySetPtr(array, arrayIndex, (void *) target);
                            AiNodeSetArray(connection.sourceNode, AtString(connection.sourceAttr.c_str()), array);
                        }
                        else if (arrayIndex >= (int) AiArrayGetNumElements(array)) {
                            unsigned numElements = AiArrayGetNumElements(array);
                            AiArrayResize(array, arrayIndex + 1, 1);
                            for (unsigned i=numElements; i<(unsigned) arrayIndex; i++)
                                AiArraySetPtr(array, i, nullptr);
                            AiArraySetPtr(array, arrayIndex, (void *) target);
                        }
                        else
                            AiArraySetPtr(array, arrayIndex, (void *)target);
                    }
                } else
                    AiNodeSetPtr(connection.sourceNode, AtString(connection.sourceAttr.c_str()), (void *)target);
            }
            else if (connection.type == ArnoldAPIAdapter::CONNECTION_LINK) {
                AtString sourceAttr(connection.sourceAttr.c_str());
                // Check if the arnold attribute is of type "node"
                const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(connection.sourceNode), sourceAttr);
                int paramType = paramEntry ? AiParamGetType(paramEntry) : AI_TYPE_NONE;
                bool isNodeAttr = paramType == AI_TYPE_NODE;

                if (isNodeAttr) {
                    // If we're trying to link a node attribute, we should just set its pointer
                    AtNode *target = _LookupTargetNodeWithAlias(connection.target.c_str(), connection.sourceNode, ArnoldAPIAdapter::CONNECTION_PTR, connection.sourceAttr);
                    AiNodeSetPtr(connection.sourceNode, AtString(connection.sourceAttr.c_str()), (void *)target);
                } else if (target == nullptr) {
                    AiNodeUnlink(connection.sourceNode, AtString(connection.sourceAttr.c_str()));
                } else {
                    static const std::string supportedElems ("xyzrgba");
                    const std::string &elem = connection.outputElement;
                    // Connection to an output component
                    const AtNodeEntry *targetEntry = AiNodeGetNodeEntry(target);
                    int noutputs = AiNodeEntryGetNumOutputs(targetEntry);

                    if (noutputs > 1 && elem.find(':') != std::string::npos)
                    {
                        std::string outputName = elem.substr(elem.find(':') + 1);
                        if (AiNodeIs(target, str::osl))
                            outputName = "param_" + outputName;

                        if (!AiNodeLinkOutput(target, outputName.c_str(), connection.sourceNode, connection.sourceAttr.c_str()))
                            AiNodeLink(target, AtString(connection.sourceAttr.c_str()), connection.sourceNode);

                    } else if (elem.length() > 1 && elem[elem.length() - 2] == ':' && supportedElems.find(elem.back()) != std::string::npos) {
                         // check for per-channel connection 
                        AiNodeLinkOutput(target, std::string(1,elem.back()).c_str(), connection.sourceNode, connection.sourceAttr.c_str());
                    } else {
                        AiNodeLink(target, AtString(connection.sourceAttr.c_str()), connection.sourceNode);
                    }
                }            
            }
        }
        return true;
    }

    // Ideally GetPrimvars shouldn't be here
    virtual const std::vector<UsdGeomPrimvar> &GetPrimvars() const = 0;
#if ARNOLD_VERSION_NUM > 70203
    const AtNodeEntry * GetCachedMtlxNodeEntry(const std::string &nodeEntryKey, const char *nodeDefinition, AtParamValueMap *params) {
        // First we check if the nodeType is an arnold shader
        std::lock_guard<AtMutex> lock(_nodeEntrymutex);
        const auto shaderNodeEntryIt = _shaderNodeEntryCache.find(nodeEntryKey);
        if (shaderNodeEntryIt == _shaderNodeEntryCache.end()) {
            // NOTE for the future: we are in lock and the following function calls the system and query the disk
            // This might be the source of contention or deadlock
            const AtNodeEntry* nodeEntry = AiMaterialxGetNodeEntryFromDefinition(nodeDefinition, params);
            _shaderNodeEntryCache[nodeEntryKey] = nodeEntry;
            return nodeEntry;
        }
        return shaderNodeEntryIt->second;
    };
#endif
#if ARNOLD_VERSION_NUM >= 70104
    AtString GetCachedOslCode(const std::string &oslCodeKey, const char *nodeDefinition, AtParamValueMap *params) {
        std::lock_guard<AtMutex> lock(_oslCodeCacheMutex);
        const auto oslCodeIt = _oslCodeCache.find(oslCodeKey);
        if (oslCodeIt == _oslCodeCache.end()) {
    #if ARNOLD_VERSION_NUM > 70104
            _oslCodeCache[oslCodeKey] = AiMaterialxGetOslShaderCode(nodeDefinition, "shader", params);
    #elif ARNOLD_VERSION_NUM >= 70104
            _oslCodeCache[oslCodeKey] = AiMaterialxGetOslShaderCode(nodeDefinition, "shader");
    #endif
        }
        return _oslCodeCache[oslCodeKey];
    }
#endif
protected:
    AtMutex _connectionMutex;
    std::vector<Connection> _connections;

    // We cache the shader's node entry and the osl code returned by the AiMaterialXxxx functions as
    // those are too costly/slow to be called for each shader prim.
    // We might want to get rid of this optimization once those functions are optimized.
#if ARNOLD_VERSION_NUM > 70203
    AtMutex _nodeEntrymutex;
    std::unordered_map<std::string, const AtNodeEntry *> _shaderNodeEntryCache;
#endif
#if ARNOLD_VERSION_NUM >= 70104
    AtMutex _oslCodeCacheMutex;
    std::unordered_map<std::string, AtString> _oslCodeCache;
#endif
    // Maps an usd path + terminal to a created node if the node path and the usd path are different
    std::unordered_map<std::pair<std::string, TfToken>, std::string, PairStringTfTokenHash> _connectionPathsAliases;

private:

    // Similar to LookupTargetNode but also search for an aliased target
    inline AtNode *_LookupTargetNodeWithAlias(const char *targetName, const AtNode *source, ConnectionType c, const std::string &sourceAttr)
    {
        // By default we optimistically look for a 1/1 mapping of arnold name to node name.
        AtNode *target = LookupTargetNode(targetName, source, c);
        // But the node might have been created on a different material terminal, so we look for those as well
        auto FindAliasWithTerminal = [&](const TfToken &terminal) {
            auto it = _connectionPathsAliases.find(std::make_pair(targetName, terminal));
            if (it != _connectionPathsAliases.end()) {
                target = LookupTargetNode(it->second.c_str(), source, c);
            }
        };
        if (target == nullptr) {
            FindAliasWithTerminal(TfToken("input"));
        }
        if (target == nullptr) { 
            FindAliasWithTerminal(TfToken(sourceAttr));
        }
        return target;
    }

    // Similar to LookupTargetNode but also search for aliased target node
    inline void _LookupTargetNodeArrayWithAlias(
        std::vector<AtNode *> &nodes, const char *targetName, const AtNode *source, ConnectionType c,
        const std::string &sourceAttr)
    {
        // By default we optimistically look for a 1/1 mapping of arnold name to node name.
        AtNode *target = LookupTargetNode(targetName, source, c);
        if (target) {
            nodes.push_back(target);
        }
        auto FindAliasWithTerminal = [&](const TfToken &terminal) {
            auto it = _connectionPathsAliases.find(std::make_pair(targetName, terminal));
            if (it != _connectionPathsAliases.end()) {
                for (const std::string &aliasPath : TfStringTokenize(it->second)) {
                    target = LookupTargetNode(aliasPath.c_str(), source, c);
                    if (target)
                        nodes.push_back(target);
                }
            }
        };
        FindAliasWithTerminal(TfToken("input"));
        FindAliasWithTerminal(TfToken(sourceAttr));
    }
};

PXR_NAMESPACE_CLOSE_SCOPE