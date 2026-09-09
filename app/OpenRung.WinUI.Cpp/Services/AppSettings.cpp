#include "pch.h"
#include "AppSettings.h"
#include "../Models/Dto.h"
#include <shlobj.h>

using namespace winrt::Windows::Data::Json;

namespace Services
{
    std::wstring AppSettings::LocalDataDir()
    {
        PWSTR known = nullptr;
        std::wstring base;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)))
        {
            base = known;
            ::CoTaskMemFree(known);
        }
        else
        {
            wchar_t buf[MAX_PATH];
            if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0)
                base = buf;
        }
        if (base.empty()) base = L".";
        return base + L"\\OpenRung";
    }

    std::wstring AppSettings::SettingsPath()
    {
        return LocalDataDir() + L"\\app-settings.json";
    }

    std::wstring AppSettings::EndpointFilePath()
    {
        // Mirrors core/server.go: %LOCALAPPDATA%\OpenRung\core-endpoint.json.
        return LocalDataDir() + L"\\core-endpoint.json";
    }

    namespace
    {
        std::wstring ReadWholeFile(std::wstring const& path)
        {
            HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return {};
            LARGE_INTEGER size{};
            if (!::GetFileSizeEx(file, &size) || size.QuadPart > 1 << 20)
            {
                ::CloseHandle(file);
                return {};
            }
            std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            ::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
            ::CloseHandle(file);
            bytes.resize(read);
            return Utf8ToWide(bytes);
        }
    }

    AppSettings AppSettings::Load()
    {
        AppSettings s;
        JsonObject obj;
        if (JsonObject::TryParse(ReadWholeFile(SettingsPath()), obj))
        {
            auto val = obj.TryLookup(L"autoClearProxy");
            if (val && val.ValueType() == JsonValueType::Boolean)
                s.autoClearProxy = val.GetBoolean();
        }
        return s;
    }

    void AppSettings::Save() const
    {
        try
        {
            auto dir = LocalDataDir();
            ::CreateDirectoryW(dir.c_str(), nullptr);

            JsonObject obj;
            obj.SetNamedValue(L"autoClearProxy", JsonValue::CreateBooleanValue(autoClearProxy));
            auto utf8 = WideToUtf8(obj.Stringify().c_str());

            HANDLE file = ::CreateFileW(SettingsPath().c_str(), GENERIC_WRITE, 0,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            ::WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            ::CloseHandle(file);
        }
        catch (...)
        {
            // best effort: preference persistence must never break the UI
        }
    }
}
