#include "stdafx.h"
#include "game_build.h"
#include "text.h"

namespace fs = std::filesystem;

namespace sod::kernel
{

    void SelectBuild(const fs::path& gameRoot)
    {
        std::error_code ec;
        const fs::path xexp = UpdateRoot(gameRoot) / "default.xexp";
        const bool installed = fs::is_regular_file(xexp, ec);
        const char* forced = getenv("SOD_BUILD");
        const bool disc = forced && _stricmp(forced, "disc") == 0;
#if SOD_HAS_TU6
        const int update = installed && !disc ? TitleUpdateNumber(xexp) : 0;
        if (installed && !disc && !update)
        {
            LOGW("Game build: {} isn't Title Update 6's or 7's; running the disc's", Utf8(xexp));
        }
        else if (update)
        {
            g_build = { update == 7 ? GameVersion::Tu7 : GameVersion::Tu6, SodBuildTu6 };
            LOGI("Game build: Title Update {} ({})", update, Utf8(UpdateRoot(gameRoot)));
            return;
        }
#else
        if (installed)
            LOGW("Game build: a title update is installed, but this executable was built without one");
#endif
        g_build = { GameVersion::Disc, SodBuildDisc };
        LOGI("Game build: disc{}", installed && disc ? " (SOD_BUILD=disc)" : "");
    }
}
