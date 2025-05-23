//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <ai_nodes.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/shader.h>

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// Version 24.11 change
#if PXR_VERSION >= 2411
inline GfVec3f MatTransform(const GfMatrix4d &mat, const GfVec3f &ptin) {
    GfVec3d ptind(ptin[0], ptin[1], ptin[2]);
    GfVec3d ptoutd = mat.Transform(ptind);
    GfVec3f ptout(static_cast<float>(ptoutd[0]), static_cast<float>(ptoutd[1]), static_cast<float>(ptoutd[2]));
    return ptout;
}
#else
inline GfVec3f MatTransform(const GfMatrix4d &mat, const GfVec3f &ptin) {
    return mat.Transform(ptin);
}
#endif

/*
 * Expands all environment variables with the form "[envar]" in the input string
 *
 * After calling this function, the output string will be a copy of the input
 * string but with all environment variables expanded with their contents.
 *
 * \param      strin  an input string containing any number of environment
 *                    variables in the form "[envar]".
 * \param[out] strout output string with all the environment variables expanded
 *                    with their contents. If any envar is not defined, it will
 *                    not be expanded. strout should have enough space to hold
 *                    the expanded string, otherwise the behaviour is undefined.
 * \return            the number of characters copied to strout.
 */
inline std::string ExpandEnvironmentVariables(const char *strin)
{
    const char *pchB;
    const char *pchE;
    const char *strI = strin;

    std::string strout = "";

    if (!strin || *strin == '\0')
        return strout;

    pchB = strchr(strin, '[');
    while (pchB != nullptr) {
        // Copy original string from last ']' to the new '['
        int nchars = pchB - strI;
        strout.append(strI, nchars);
        strI = pchB;
        pchE = strchr(pchB + 1, ']');
        if (pchE == nullptr)
            break;

        // Found a [var] token. Recover its name.
        nchars = pchE - pchB - 1;
        if (nchars <= 0)
            break;

        std::string envarStr(pchB + 1, nchars);
        std::string envar;
        const char *envar_char = std::getenv(envarStr.c_str());
        // If the envar is defined, then expand its content
        if (envar_char) {
            strout.append(std::string(envar_char));
        } else {
            nchars = pchE - pchB + 1;
            strout.append(pchB, nchars);
        }

        // Look for the next environment variable
        strI = pchE + 1;
        pchB = strchr(pchE + 1, '[');
    }

    // Copy the remaining original string
    strout.append(strI);

    return strout;
}

#if defined(_WIN32)

/*
 * Fix for win32, where POSIX function strtok_r is called strtok_s, see:
 * http://msdn.microsoft.com/en-us/library/ftsafwz3(v=vs.80).aspx
 * http://stackoverflow.com/questions/9021502/whats-the-difference-between-strtok-r-and-strtok-s-in-c
 */
#define strtok_r strtok_s
#endif

//-*****************************************************************************
static char empty[] = "";

inline void TokenizePath(
    const std::string &path, std::vector<std::string> &result, const std::string &sep, bool filepath)
{
    char *token, *paramStr = strdup(path.c_str());
    char *savept;
#ifdef _WIN32
    char *lastToken = empty;
#endif
    token = strtok_r(paramStr, sep.c_str(), &savept);
    while (token != nullptr) {
        std::string opath = std::string(token);
#ifdef _WIN32
        // On Windows, we might see something like "a:foo" and any human
        // would know that it means drive/directory 'a:foo', NOT
        // separate directories 'a' and 'foo'.  Implement the obvious
        // heuristic here.  Note that this means that we simply don't
        // correctly support searching in *relative* directories that
        // consist of a single letter.
        if (filepath && strlen(lastToken) == 1 && lastToken[0] != '.') {
            // If the last token was a single letter, try prepending it
            opath = std::string(lastToken) + ":" + (token);
        } else
#endif
            opath = std::string(token);

        size_t len = opath.length();
        while (len > 1 && (opath[len - 1] == '/' || opath[len - 1] == '\\'))
            opath.erase(--len);

        result.push_back(opath);
#ifdef _WIN32
        lastToken = token;
#endif
        token = strtok_r(nullptr, sep.c_str(), &savept);
    }
    free(paramStr);
}

/*
 * Returns "true" if the given path is not empty and it doesn't contain a trailing slash
 * (or backslash, depending on platform).
 */
inline bool PathNeedsTrailingSlash(const char *path)
{
    int len = strlen(path);
#ifdef _WIN32
    return ((len > 0) && (path[len - 1] != '/') && (path[len - 1] != '\\'));
#else
    return ((len > 0) && (path[len - 1] != '/'));
#endif
}

/*
 * Joins a directory path and filename
 *
 * \param     dirpath  string containing a path to a directory
 * \param     filename string containing a filename
 *
 * \return             file path joining directory and filename
 */
inline std::string PathJoin(const char *dirpath, const char *filename)
{
    if (PathNeedsTrailingSlash(dirpath))
        return (std::string(dirpath) + "/") + filename;
    else
        return std::string(dirpath) + filename;
}

/*
 * Returns "true" if the given filename exists and is accessible
 */
inline bool IsFileAccessible(const std::string &filename)
{
    FILE *pFile;
    pFile = fopen(filename.c_str(), "r");
    if (pFile) {
        fclose(pFile);
        return true;
    }

    return false;
}
