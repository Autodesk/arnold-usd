// Copyright 2019 Autodesk, Inc.
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

// convert from "snake_case" to "camelCase"
// ignores the capitalization of input strings: letters are only capitalized
// if they follow an underscore
//
inline std::string makeCamelCase(const std::string &in)
{
    std::string out;
    out.reserve(in.length());
    bool capitalize = false;
    unsigned char c;
    for (size_t i = 0; i < in.length(); ++i) {
        c = in[i];
        if (c == '_') {
            capitalize = true;
        } else {
            if (capitalize) {
                c = toupper(c);
                capitalize = false;
            }
            out += c;
        }
    }
    return out;
}

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
inline std::string expandEnvironmentVariables(const char * strin)
{
    const char *pch_b;
    const char *pch_e;
    const char *str_i = strin;

    std::string strout = "";

    if (!strin || *strin == '\0') 
        return strout;

    pch_b = strchr(strin, '[');
    while (pch_b != nullptr) {
        // Copy original string from last ']' to the new '['
        int nchars = pch_b - str_i;
        strout.append(str_i, nchars);
        str_i = pch_b;
        pch_e = strchr(pch_b + 1, ']');
        if (pch_e == nullptr) 
            break;

        // Found a [var] token. Recover its name.
        nchars = pch_e - pch_b - 1;
        if (nchars <= 0) 
            break;

        std::string envar_str(pch_b + 1, nchars);
        std::string envar;
        const char *envar_char = std::getenv(envar_str.c_str());
        // If the envar is defined, then expand its content
        if (envar_char) {
            strout.append(std::string(envar_char));
        }
        else {
            nchars = pch_e - pch_b + 1;
            strout.append(pch_b, nchars);
        }

        // Look for the next environment variable
        str_i = pch_e + 1;
        pch_b = strchr(pch_e + 1, '[');
    }

    //Copy the remaining original string
    strout.append(str_i);

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

inline void tokenizePath(const std::string & path, std::vector<std::string> & result, const std::string& sep, bool filepath)
{
    char *token, *param_str = strdup(path.c_str());
    char* savept;
    char* last_token = empty;
    token = strtok_r(param_str, sep.c_str(), &savept);
    while (token != nullptr) {
        std::string opath = std::string(token);
#ifdef _WIN32
        // On Windows, we might see something like "a:foo" and any human
        // would know that it means drive/directory 'a:foo', NOT
        // separate directories 'a' and 'foo'.  Implement the obvious
        // heuristic here.  Note that this means that we simply don't
        // correctly support searching in *relative* directories that
        // consist of a single letter.
        if (filepath && strlen(last_token) == 1 && last_token[0] != '.') {
            // If the last token was a single letter, try prepending it
            opath = std::string(last_token) + ":" + (token);
        } else
#endif
            opath = std::string(token);

        size_t len = opath.length();
        while (len > 1 && (opath[len-1] == '/' || opath[len-1] == '\\'))
            opath.erase (--len);

        result.push_back(opath);
        last_token = token;
        token = strtok_r(nullptr, sep.c_str(), &savept);
    }
    free(param_str);
}

/*
 * Returns "true" if the given path is not empty and it doesn't contain a trailing slash
 * (or backslash, depending on platform).
 */
inline bool pathNeedsTrailingSlash(const char * path)
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
inline std::string pathJoin(const char * dirpath, const char * filename)
{
    if (pathNeedsTrailingSlash(dirpath))
        return (std::string(dirpath) + "/") + filename;
    else
        return std::string(dirpath) + filename;
}


/*
 * Returns "true" if the given filename exists and is accessible
 */
inline bool isFileAccessible(const std::string & filename)
{
    FILE * pFile;
    pFile = fopen(filename.c_str(), "r");
    if (pFile)
    {
        fclose(pFile);
        return true;
    }

    return false;
}
