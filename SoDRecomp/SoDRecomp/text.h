#pragma once
// Text helpers. Paths go to and from UTF-8 here rather than with path::string(),
// which is in the ANSI code page and throws for the characters it lacks.

namespace sod
{
#ifdef _WIN32
    // UTF-16 as UTF-8 (unpaired surrogates, which Windows names may have, become U+FFFD).
    inline std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    // Text in `codePage` as UTF-16 (invalid bytes become U+FFFD).
    inline std::wstring ToWide(std::string_view text, UINT codePage = CP_UTF8)
    {
        if (text.empty())
            return {};
        const int size = MultiByteToWideChar(codePage, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring out(size, L'\0');
        MultiByteToWideChar(codePage, 0, text.data(), int(text.size()), out.data(), size);
        return out;
    }

    inline bool IsUtf8(std::string_view text)
    {
        return text.empty() ||
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0) > 0;
    }

    inline std::string Utf8(const std::filesystem::path& path)
    {
        return WideToUtf8(path.native());
    }

    inline std::filesystem::path FromUtf8(std::string_view text)
    {
        return ToWide(text);
    }

    // A name in the ANSI code page, as the guest's file names are (guest paths become
    // host paths through it); nothing if it has characters the code page lacks.
    inline std::optional<std::string> ToAnsi(const std::filesystem::path& name)
    {
        const std::wstring& text = name.native();
        if (text.empty())
            return std::string();
        BOOL lossy = FALSE;
        const int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), int(text.size()), nullptr, 0,
            nullptr, &lossy);
        if (size <= 0 || lossy)
            return std::nullopt;
        std::string out(size, '\0');
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), int(text.size()), out.data(), size, nullptr,
            nullptr);
        return out;
    }
#else
    // POSIX: paths are UTF-8 bytes already; wchar_t is UTF-32 (the wide helpers exist
    // for the code that converts the game's UTF-16 text through them).
    inline std::string WideToUtf8(std::wstring_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++)
        {
            uint32_t c = uint32_t(text[i]);
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && uint32_t(text[i + 1]) >= 0xDC00 &&
                uint32_t(text[i + 1]) <= 0xDFFF)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00);
                i++;
            }
            else if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
                c = 0xFFFD;
            if (c < 0x80)
                out += char(c);
            else if (c < 0x800)
                out += { char(0xC0 | (c >> 6)), char(0x80 | (c & 0x3F)) };
            else if (c < 0x10000)
                out += { char(0xE0 | (c >> 12)), char(0x80 | ((c >> 6) & 0x3F)), char(0x80 | (c & 0x3F)) };
            else
                out += { char(0xF0 | (c >> 18)), char(0x80 | ((c >> 12) & 0x3F)), char(0x80 | ((c >> 6) & 0x3F)),
                    char(0x80 | (c & 0x3F)) };
        }
        return out;
    }

    // Decodes UTF-8; `valid` is cleared on a malformed sequence (which becomes U+FFFD).
    inline std::wstring DecodeUtf8(std::string_view text, bool& valid)
    {
        std::wstring out;
        out.reserve(text.size());
        valid = true;
        for (size_t i = 0; i < text.size();)
        {
            const uint8_t b = uint8_t(text[i]);
            uint32_t c;
            size_t n;
            if (b < 0x80) { c = b; n = 1; }
            else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; n = 2; }
            else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; n = 3; }
            else if ((b & 0xF8) == 0xF0) { c = b & 0x07; n = 4; }
            else { valid = false; out += wchar_t(0xFFFD); i++; continue; }
            if (i + n > text.size()) { valid = false; out += wchar_t(0xFFFD); break; }
            bool ok = true;
            for (size_t k = 1; k < n; k++)
            {
                const uint8_t t = uint8_t(text[i + k]);
                if ((t & 0xC0) != 0x80) { ok = false; break; }
                c = (c << 6) | (t & 0x3F);
            }
            if (!ok || (n == 2 && c < 0x80) || (n == 3 && c < 0x800) || (n == 4 && c < 0x10000) || c > 0x10FFFF ||
                (c >= 0xD800 && c <= 0xDFFF))
            {
                valid = false;
                out += wchar_t(0xFFFD);
                i++;
                continue;
            }
            out += wchar_t(c);
            i += n;
        }
        return out;
    }

    inline std::wstring ToWide(std::string_view text, UINT = CP_UTF8)
    {
        bool valid;
        return DecodeUtf8(text, valid);
    }

    inline bool IsUtf8(std::string_view text)
    {
        bool valid;
        DecodeUtf8(text, valid);
        return valid;
    }

    inline std::string Utf8(const std::filesystem::path& path)
    {
        return path.native();
    }

    inline std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::string(text));
    }

    // No ANSI code page here: file names are UTF-8 as they are.
    inline std::optional<std::string> ToAnsi(const std::filesystem::path& name)
    {
        return name.native();
    }
#endif

    // ASCII lowercase.
    inline std::string Lower(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
            c = char(tolower(uint8_t(c)));
        return out;
    }
}
