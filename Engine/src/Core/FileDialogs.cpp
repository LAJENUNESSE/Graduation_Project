#include "engpch.h"
#include "Core/FileDialogs.h"

#include <tinyfiledialogs.h>

namespace Engine
{

    // Split semicolon-separated filter string (e.g. "*.png;*.jpg") into a vector of C strings
    static std::vector<const char*> SplitFilter(const char* filter, std::vector<std::string>& storage)
    {
        std::vector<const char*> result;
        if (!filter)
            return result;

        std::string s(filter);
        size_t start = 0;
        while (start < s.size())
        {
            size_t end = s.find(';', start);
            if (end == std::string::npos)
                end = s.size();
            storage.push_back(s.substr(start, end - start));
            start = end + 1;
        }

        for (auto& str : storage)
            result.push_back(str.c_str());

        return result;
    }

    std::string FileDialogs::OpenFile(const char* filter, const char* description)
    {
#ifdef _WIN32
        tinyfd_winUtf8 = 1;
#endif
        std::vector<std::string> storage;
        auto patterns = SplitFilter(filter, storage);

        const char* result = tinyfd_openFileDialog("打开文件",                        // title
                                                   "",                                // default path
                                                   static_cast<int>(patterns.size()), // number of filter patterns
                                                   patterns.empty() ? nullptr : patterns.data(), // filter patterns
                                                   description,                                  // filter description
                                                   0 // allow multiple selects
        );

        return result ? std::string(result) : std::string();
    }

    std::string FileDialogs::SaveFile(const char* filter, const char* description)
    {
#ifdef _WIN32
        tinyfd_winUtf8 = 1;
#endif
        std::vector<std::string> storage;
        auto patterns = SplitFilter(filter, storage);

        const char* result = tinyfd_saveFileDialog("保存文件",                        // title
                                                   "",                                // default path
                                                   static_cast<int>(patterns.size()), // number of filter patterns
                                                   patterns.empty() ? nullptr : patterns.data(), // filter patterns
                                                   description                                   // filter description
        );

        return result ? std::string(result) : std::string();
    }

} // namespace Engine
