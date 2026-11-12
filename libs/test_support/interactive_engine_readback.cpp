//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
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

// Python module of the interactive render delegate tests (testsuite/common/interactive_engine_test.py): reads back an
// AOV of a UsdImagingGL.Engine, which USD doesn't wrap in python (UsdImagingGLEngine::GetAovRenderBuffer).
//
// It must be built against the same USD as the pxr modules that import it, so that the UsdImagingGL.Engine python
// object converts to the C++ engine through the boost python converter registry of that USD. It doesn't link Arnold
// nor the render delegate, and it has no rpath: libusd is already loaded by the pxr modules when it's imported.
#include <pxr/pxr.h>
#include <pxr/base/gf/half.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/usdImaging/usdImagingGL/engine.h>

// USD embeds its own copy of boost python since 24.11
#if PXR_VERSION >= 2411
#include <pxr/external/boost/python.hpp>
namespace bp = PXR_BOOST_NAMESPACE::python;
#define READBACK_PYTHON_MODULE PXR_BOOST_PYTHON_MODULE
#else
#include <boost/python.hpp>
namespace bp = boost::python;
#define READBACK_PYTHON_MODULE BOOST_PYTHON_MODULE
#endif

#include <cstdint>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

void RaiseRuntimeError(const std::string& message)
{
    PyErr_SetString(PyExc_RuntimeError, message.c_str());
    bp::throw_error_already_set();
}

const char* GetComponentFormatName(HdFormat componentFormat)
{
    switch (componentFormat) {
        case HdFormatFloat32:
            return "float32";
        case HdFormatFloat16:
            return "float16";
        case HdFormatUNorm8:
            return "unorm8";
        case HdFormatInt32:
            return "int32";
        default:
            return nullptr;
    }
}

// Returns (width, height, format, bytes) for the AOV of the engine: bytes holds width * height RGBA float32 pixels,
// top-down rows. Missing components are 0, and alpha is 1 when the AOV has fewer than 4 components. format is the
// format of the render buffer, e.g. "float32x4" for the color or "int32x1" for primId. int32 values are exact up to
// 2^24. Raises a RuntimeError when the engine has no buffer for the AOV or the format isn't supported.
bp::tuple ReadAov(UsdImagingGLEngine& engine, const std::string& aovName)
{
    HdRenderBuffer* buffer = engine.GetAovRenderBuffer(TfToken(aovName));
    if (!buffer) {
        RaiseRuntimeError("No render buffer for the AOV " + aovName);
    }
    buffer->Resolve();
    const int width = static_cast<int>(buffer->GetWidth());
    const int height = static_cast<int>(buffer->GetHeight());
    const HdFormat format = buffer->GetFormat();
    const int components = static_cast<int>(HdGetComponentCount(format));
    const HdFormat componentFormat = HdGetComponentFormat(format);
    const char* componentFormatName = GetComponentFormatName(componentFormat);
    if (!componentFormatName) {
        RaiseRuntimeError("Unsupported format " + std::to_string(static_cast<int>(format)) + " for the AOV " + aovName);
    }

    std::vector<float> rgba(static_cast<size_t>(width) * height * 4, 0.f);
    const void* data = buffer->Map();
    if (!data) {
        RaiseRuntimeError("The render buffer of the AOV " + aovName + " can't be mapped");
    }
    for (int y = 0; y < height; ++y) {
        // Hydra render buffers are stored bottom-up
        const size_t srcRow = static_cast<size_t>(height - 1 - y) * width;
        for (int x = 0; x < width; ++x) {
            const size_t src = (srcRow + x) * components;
            float* dst = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            dst[3] = 1.f;
            for (int c = 0; c < components && c < 4; ++c) {
                switch (componentFormat) {
                    case HdFormatFloat32:
                        dst[c] = static_cast<const float*>(data)[src + c];
                        break;
                    case HdFormatFloat16:
                        dst[c] = static_cast<float>(static_cast<const GfHalf*>(data)[src + c]);
                        break;
                    case HdFormatUNorm8:
                        dst[c] = static_cast<const uint8_t*>(data)[src + c] / 255.f;
                        break;
                    case HdFormatInt32:
                        dst[c] = static_cast<float>(static_cast<const int32_t*>(data)[src + c]);
                        break;
                    default:
                        break;
                }
            }
        }
    }
    buffer->Unmap();

    PyObject* bytes = PyBytes_FromStringAndSize(
        reinterpret_cast<const char*>(rgba.data()), static_cast<Py_ssize_t>(rgba.size() * sizeof(float)));
    if (!bytes) {
        bp::throw_error_already_set();
    }
    return bp::make_tuple(
        width, height, std::string(componentFormatName) + "x" + std::to_string(components),
        bp::object(bp::handle<>(bytes)));
}

} // namespace

READBACK_PYTHON_MODULE(interactive_engine_readback)
{
    bp::def("ReadAov", &ReadAov, (bp::arg("engine"), bp::arg("aov")));
}
