#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mb {

// The game's text in every language, from Data/xml/languages.json (written by tools/ui.py from
// languages.xml; schema in docs/ASSET_FORMATS.md). Strings are UTF-8. Keys are case-sensitive.
class StringTable {
public:
    static StringTable load(const std::filesystem::path& languagesJson);  // throws on error
    static StringTable parse(std::string_view json);                      // throws on error

    // The language in use: the file's CurrentLanguage until setLanguage() picks another.
    const std::string& language() const { return languages_[current_].name; }
    std::vector<std::string> languages() const;
    // Case-insensitive. Returns false (and changes nothing) if the table has no such language.
    bool setLanguage(std::string_view name);

    // The string called `key` in the current language, or in the first language if the current one lacks
    // it. Null if neither has it.
    const std::string* find(std::string_view key) const;

    // Form text is either a key ("StartGame") or literal text ("Quick Start", "1000"): the string of the key
    // if there is one, else the text as it was written.
    std::string resolve(std::string_view keyOrText) const;

private:
    struct Language {
        std::string name;
        std::unordered_map<std::string, std::string> strings;
    };
    std::vector<Language> languages_;  // never empty
    std::size_t current_ = 0;
};

}  // namespace mb
