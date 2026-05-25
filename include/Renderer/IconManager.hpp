#pragma once

#include <d3d11.h>

#include <string>
#include <unordered_map>

class IconManager {
public:
    explicit IconManager(ID3D11Device* device);
    ~IconManager();

    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;

    bool LoadAll();
    ID3D11ShaderResourceView* GetIcon(const std::string& name) const;
    void Shutdown();

private:
    struct IconResource {
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int width = 0;
        int height = 0;
    };

    bool LoadIconFile(const std::wstring& directory, const std::wstring& filename);
    std::wstring FindIconDirectory() const;
    void ReleaseResource(IconResource& resource);

    ID3D11Device* m_device = nullptr;
    std::unordered_map<std::string, IconResource> m_icons;
};
