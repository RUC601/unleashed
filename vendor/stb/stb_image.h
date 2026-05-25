#pragma once

#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

typedef unsigned char stbi_uc;

#ifdef __cplusplus
extern "C" {
#endif

stbi_uc* stbi_load(char const* filename, int* x, int* y, int* channels_in_file, int desired_channels);
void stbi_image_free(void* retval_from_stbi_load);
const char* stbi_failure_reason(void);

#ifdef __cplusplus
}
#endif

#ifdef STB_IMAGE_IMPLEMENTATION

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <string>

static thread_local const char* stbi__last_failure = nullptr;

static stbi_uc* stbi__fail(const char* reason) {
    stbi__last_failure = reason;
    return nullptr;
}

static std::wstring stbi__wide_from_codepage(const char* text, UINT code_page, DWORD flags) {
    if (!text)
        return {};

    int count = MultiByteToWideChar(code_page, flags, text, -1, nullptr, 0);
    if (count <= 0)
        return {};

    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(code_page, flags, text, -1, result.data(), count);
    if (!result.empty() && result.back() == L'\0')
        result.pop_back();
    return result;
}

static std::wstring stbi__wide_from_filename(const char* filename) {
    std::wstring result = stbi__wide_from_codepage(filename, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!result.empty())
        return result;
    return stbi__wide_from_codepage(filename, CP_ACP, 0);
}

class stbi__com_scope {
public:
    stbi__com_scope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == S_OK || hr == S_FALSE) {
            initialized_ = true;
            ok_ = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            ok_ = true;
        } else {
            ok_ = false;
        }
    }

    ~stbi__com_scope() {
        if (initialized_)
            CoUninitialize();
    }

    bool ok() const { return ok_; }

private:
    bool initialized_ = false;
    bool ok_ = false;
};

extern "C" const char* stbi_failure_reason(void) {
    return stbi__last_failure ? stbi__last_failure : "no failure";
}

extern "C" void stbi_image_free(void* retval_from_stbi_load) {
    std::free(retval_from_stbi_load);
}

extern "C" stbi_uc* stbi_load(char const* filename, int* x, int* y, int* channels_in_file, int desired_channels) {
    if (!filename)
        return stbi__fail("filename is null");
    if (desired_channels < 0 || desired_channels > 4)
        return stbi__fail("unsupported channel count");

    const std::wstring wide_filename = stbi__wide_from_filename(filename);
    if (wide_filename.empty())
        return stbi__fail("failed to convert filename");

    stbi__com_scope com;
    if (!com.ok())
        return stbi__fail("failed to initialize COM");

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr))
        return stbi__fail("failed to create WIC factory");

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        wide_filename.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    if (FAILED(hr))
        return stbi__fail("failed to create WIC decoder");

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
        return stbi__fail("failed to decode first image frame");

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
        return stbi__fail("invalid image dimensions");
    if (width > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<int>::max()))
        return stbi__fail("image dimensions exceed int range");
    if (width > std::numeric_limits<UINT>::max() / 4)
        return stbi__fail("image row is too large");

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
        return stbi__fail("failed to create WIC format converter");

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr))
        return stbi__fail("failed to convert image to RGBA");

    const UINT rgba_stride = width * 4;
    const size_t rgba_size = static_cast<size_t>(rgba_stride) * static_cast<size_t>(height);
    if (rgba_stride != 0 && rgba_size / rgba_stride != height)
        return stbi__fail("image buffer is too large");

    std::vector<stbi_uc> rgba(rgba_size);
    hr = converter->CopyPixels(nullptr, rgba_stride, static_cast<UINT>(rgba.size()), rgba.data());
    if (FAILED(hr))
        return stbi__fail("failed to copy image pixels");

    const int source_channels = 4;
    const int output_channels = desired_channels > 0 ? desired_channels : source_channels;
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixel_count != 0 && pixel_count > std::numeric_limits<size_t>::max() / static_cast<size_t>(output_channels))
        return stbi__fail("output image buffer is too large");

    const size_t output_size = pixel_count * static_cast<size_t>(output_channels);
    stbi_uc* output = static_cast<stbi_uc*>(std::malloc(output_size));
    if (!output)
        return stbi__fail("out of memory");

    if (output_channels == 4) {
        std::memcpy(output, rgba.data(), output_size);
    } else {
        for (size_t i = 0; i < pixel_count; ++i) {
            const stbi_uc r = rgba[i * 4 + 0];
            const stbi_uc g = rgba[i * 4 + 1];
            const stbi_uc b = rgba[i * 4 + 2];
            const stbi_uc a = rgba[i * 4 + 3];
            const stbi_uc lum = static_cast<stbi_uc>((static_cast<unsigned>(r) * 77u +
                                                      static_cast<unsigned>(g) * 150u +
                                                      static_cast<unsigned>(b) * 29u) >> 8);

            stbi_uc* dst = output + i * output_channels;
            switch (output_channels) {
                case 1:
                    dst[0] = lum;
                    break;
                case 2:
                    dst[0] = lum;
                    dst[1] = a;
                    break;
                case 3:
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    break;
                default:
                    break;
            }
        }
    }

    if (x)
        *x = static_cast<int>(width);
    if (y)
        *y = static_cast<int>(height);
    if (channels_in_file)
        *channels_in_file = source_channels;

    stbi__last_failure = nullptr;
    return output;
}

#endif // STB_IMAGE_IMPLEMENTATION
#endif // STBI_INCLUDE_STB_IMAGE_H
