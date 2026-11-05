// Cheats: stubbed for State of Decay. DAORecomp's version ran Dragon Age: Origins'
// own console commands through DAO's script engine at DAO's addresses; SoD has none
// wired up, so cheats are unavailable.
#include "stdafx.h"
#include "ui/cheats.h"

namespace sod::ui
{
    void ToggleCheats()
    {
    }

    bool CheatsOpen()
    {
        return false;
    }

    bool CheatsAvailable()
    {
        return false;
    }

    uint16_t CheatsFilterButtons(uint16_t buttons)
    {
        return buttons;
    }

    void UpdateCheats(uint8_t*, int32_t, uint32_t)
    {
    }

    namespace cheats
    {
        void Run(std::string_view, std::string_view, Scope)
        {
        }

        State GetState()
        {
            return {};
        }
    }
}
