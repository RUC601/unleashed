#include "Renderer/IconManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <limits>
#include <string>

#include "stb_image.h"

namespace {

bool DirectoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return {};

    std::wstring directory(path, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};

    directory.resize(slash);
    return directory;
}

std::wstring StemFromFilename(const std::wstring& filename) {
    std::wstring stem = filename;
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos)
        stem.resize(dot);
    return stem;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty())
        return {};

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};

    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), bytes, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}

} // namespace

IconManager::IconManager(ID3D11Device* device)
    : m_device(device) {
    if (m_device)
        m_device->AddRef();
}

IconManager::~IconManager() {
    Shutdown();

    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
}

bool IconManager::LoadAll() {
    Shutdown();

    if (!m_device)
        return false;

    const std::wstring directory = FindIconDirectory();
    if (directory.empty())
        return false;

    WIN32_FIND_DATAW find_data = {};
    const std::wstring search_pattern = directory + L"\\*.png";
    HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE)
        return false;

    int loaded_count = 0;
    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;

        if (LoadIconFile(directory, find_data.cFileName))
            ++loaded_count;
    } while (FindNextFileW(find_handle, &find_data));

    FindClose(find_handle);
    return loaded_count > 0;
}

ID3D11ShaderResourceView* IconManager::GetIcon(const std::string& name) const {
    const auto it = m_icons.find(name);
    return it != m_icons.end() ? it->second.srv : nullptr;
}

void IconManager::Shutdown() {
    for (auto& item : m_icons)
        ReleaseResource(item.second);
    m_icons.clear();
}

bool IconManager::LoadIconFile(const std::wstring& directory, const std::wstring& filename) {
    const std::wstring full_path_wide = directory + L"\\" + filename;
    const std::string full_path = WideToUtf8(full_path_wide);
    if (full_path.empty())
        return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(full_path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels)
            stbi_image_free(pixels);
        return false;
    }

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = static_cast<UINT>(width);
    texture_desc.Height = static_cast<UINT>(height);
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = pixels;
    init_data.SysMemPitch = static_cast<UINT>(width * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&texture_desc, &init_data, &texture);
    stbi_image_free(pixels);

    if (FAILED(hr) || !texture)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = texture_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = m_device->CreateShaderResourceView(texture, &srv_desc, &srv);
    if (FAILED(hr) || !srv) {
        texture->Release();
        return false;
    }

    IconResource resource = {};
    resource.texture = texture;
    resource.srv = srv;
    resource.width = width;
    resource.height = height;

    const std::string key = WideToUtf8(StemFromFilename(filename));
    if (key.empty()) {
        ReleaseResource(resource);
        return false;
    }

    IconResource& slot = m_icons[key];
    ReleaseResource(slot);
    slot = resource;
    return true;
}

std::wstring IconManager::FindIconDirectory() const {
    const std::wstring working_directory_assets = L"assets\\icons";
    if (DirectoryExists(working_directory_assets))
        return working_directory_assets;

    const std::wstring exe_directory = ExecutableDirectory();
    if (!exe_directory.empty()) {
        const std::wstring executable_assets = exe_directory + L"\\assets\\icons";
        if (DirectoryExists(executable_assets))
            return executable_assets;
    }

    return {};
}

void IconManager::ReleaseResource(IconResource& resource) {
    if (resource.srv) {
        resource.srv->Release();
        resource.srv = nullptr;
    }
    if (resource.texture) {
        resource.texture->Release();
        resource.texture = nullptr;
    }
    resource.width = 0;
    resource.height = 0;
}
