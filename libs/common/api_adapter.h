//
// SPDX-License-Identifier: Apache-2.0
//


#pragma once

#include <pxr/usd/usdGeom/primvar.h>
#include "constant_strings.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

// This is a base class used to call Arnold API functions within a particular context.
// For example we might want to wrap the AiNode call and add mutex, or store the nodes dependending on the context.
class ArnoldAPIAdapter {
public:

    ArnoldAPIAdapter() {}
    virtual ~ArnoldAPIAdapter() {}
    
    // Type of connection between 2 nodes
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
    virtual bool ProcessConnection(const Connection& connection)
    {
        if (connection.type == ArnoldAPIAdapter::CONNECTION_ARRAY) {
            std::vector<AtNode *> vecNodes;
            std::stringstream ss(connection.target);
            std::string token;
            while (std::getline(ss, token, ' ')) {
                AtNode *target = LookupTargetNode(token.c_str(), connection.sourceNode, connection.type);
                if (target == nullptr)
                    return false; // node is missing, we don't process the connection                
                vecNodes.push_back(target);
            }
            AiNodeSetArray(
                connection.sourceNode, AtString(connection.sourceAttr.c_str()),
                AiArrayConvert(vecNodes.size(), 1, AI_TYPE_NODE, &vecNodes[0]));
        } else {
            AtNode *target = LookupTargetNode(connection.target.c_str(), connection.sourceNode, connection.type);
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
                    AtNode *target = LookupTargetNode(connection.target.c_str(), connection.sourceNode, ArnoldAPIAdapter::CONNECTION_PTR);
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
};

PXR_NAMESPACE_CLOSE_SCOPE