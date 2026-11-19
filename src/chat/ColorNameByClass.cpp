// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `SetChatColorNameByClass(chatType, colorNameByClass)` and its event
// UPDATE_CHAT_COLOR_NAME_BY_CLASS(chatType, colorNameByClass).
//
// 4.3.4 keeps the option as a byte on each chat colour entry (ChatColorInfo
// +0x48), beside the colour ChangeChatColor writes, and handles it at four
// points of its chat code:
//   - the chat start-up seeds it from the static defaults (all off);
//   - the chat-cache.txt reader takes a fifth COLORS word, Y or N (any other
//     word leaves the byte), and fires the event for every entry after that
//     entry's UPDATE_CHAT_COLOR;
//   - the writer ends each COLORS line with Y or N;
//   - Script_SetChatColorNameByClass (0x008EA240) sets it on the matching
//     registry entry and the matching extra channel entry, and fires the
//     event when either matched.
// ResetChatColors leaves it alone. The client only stores and announces the
// option; FrameXML's GetColoredName colours the names.
//
// 1.12's entries are 0x43 bytes with no room for the byte (benilla-re
// ui/chatcolors), so the flags live here in the registry's order, and each of
// the four points is the 1.12 counterpart of 4.3.4's:
//   - FUN_CHAT_INITIALIZE_GAME clears them before it runs, because the
//     settings reader it registers can run before it returns;
//   - the COLORS arm reads the fifth word right after the blue one
//     (CALL_CHAT_SETTINGS_READ_COLOR_BLUE);
//   - FUN_CHAT_SETTINGS_LOAD fires the event for every entry after its own
//     UPDATE_CHAT_COLOR pass. 4.3.4 alternates the two per entry; neither
//     handler reads the other's value, so the order does not matter;
//   - both COLORS line writes add the word (CALL_CHAT_SETTINGS_WRITE_*).
// A stock 1.12 reader takes three words of a COLORS line and goes to the next
// line, so the fifth word is harmless to a client without this DLL.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Chat::ColorNameByClass {

namespace {

using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
using SStrPrintf_t = int(__cdecl *)(char *dest, uint32_t maxChars, const char *format, ...);
using SStrToInt_t = int(__stdcall *)(const char *string);
using SStrTokenize_t = void(__stdcall *)(const char **cursor, char *out, uint32_t outSize,
                                         const char *delims, int *outQuoted);
using InitializeGame_t = void(__cdecl *)();
using SettingsLoad_t = void(__fastcall *)(int ecx, int file, void *arg);

// 4.3.4's STORM_MAX_STR, the length its name matches pass.
constexpr int kMaxStr = 0x7FFFFFFF;

// One flag per registry entry, then one per extra channel entry. The extra
// array can grow, so its flags grow with it on first use.
bool g_registry[Offsets::CHAT_TYPE_COUNT] = {};
std::vector<bool> g_channels;

const uint8_t *Registry() { return reinterpret_cast<const uint8_t *>(Offsets::VAR_CHAT_COLORS); }

const uint8_t *Channels() {
    return Game::Read<const uint8_t *>(Offsets::VAR_CHAT_CHANNEL_COLORS_PTR);
}

uint32_t ChannelCount() { return Game::Read<uint32_t>(Offsets::VAR_CHAT_CHANNEL_COLORS_COUNT); }

std::vector<bool>::reference ChannelFlag(uint32_t index) {
    if (g_channels.size() <= index)
        g_channels.resize(index + 1, false);
    return g_channels[index];
}

// The flag of the entry that starts at `entry`, or nullptr when no entry
// starts there.
bool *RegistryFlagAt(const void *entry) {
    const auto offset = static_cast<const uint8_t *>(entry) - Registry();
    if (offset < 0 || offset >= Offsets::CHAT_TYPE_COUNT * Offsets::CHAT_COLOR_STRIDE ||
        offset % Offsets::CHAT_COLOR_STRIDE != 0)
        return nullptr;
    return &g_registry[offset / Offsets::CHAT_COLOR_STRIDE];
}

// The extra-channel index of the entry that starts at `entry`, or -1.
int ChannelIndexAt(const void *entry) {
    const uint8_t *channels = Channels();
    if (channels == nullptr)
        return -1;
    const auto offset = static_cast<const uint8_t *>(entry) - channels;
    if (offset < 0 || offset >= static_cast<ptrdiff_t>(ChannelCount()) * Offsets::CHAT_COLOR_STRIDE ||
        offset % Offsets::CHAT_COLOR_STRIDE != 0)
        return -1;
    return static_cast<int>(offset / Offsets::CHAT_COLOR_STRIDE);
}

bool FlagAt(const void *entry) {
    if (const bool *flag = RegistryFlagAt(entry))
        return *flag;
    const int channel = ChannelIndexAt(entry);
    return channel >= 0 && ChannelFlag(static_cast<uint32_t>(channel));
}

void SetFlagAt(const void *entry, bool on) {
    if (bool *flag = RegistryFlagAt(entry)) {
        *flag = on;
        return;
    }
    const int channel = ChannelIndexAt(entry);
    if (channel >= 0)
        ChannelFlag(static_cast<uint32_t>(channel)) = on;
}

// --- the event --------------------------------------------------------------

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("name", "cstring"),
    Game::Doc::Req("colorNameByClass", "bool"),
};
const Game::Doc::Event kEventDoc{
    "ChatInfo",
    "Fires when a chat type's option to show sender names in their class colour is set, "
    "and for every chat type when the chat settings load.",
    kPayload};
const Event::Custom::AutoReserve kEvent{"UPDATE_CHAT_COLOR_NAME_BY_CLASS", &kEventDoc};

// `name` is the entry's own name (the event's first argument is a string the
// handler may keep, and the engine copies nothing).
void Fire(const char *name, bool on) {
    const int event = kEvent.Slot();
    if (!Event::Custom::HasListeners(event))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, name);
    Game::Lua::PushBool(L, on);
    Event::Custom::FireValues(event, L, top + 1, 2);
    Game::Lua::SetTop(L, top);
}

// --- chat start-up and the settings file -----------------------------------

InitializeGame_t g_origInitializeGame = nullptr;

void __cdecl InitializeGame_h() {
    for (bool &flag : g_registry)
        flag = false;
    g_channels.clear();
    g_origInitializeGame();
}

const Game::HookAutoRegister _initHook{Offsets::FUN_CHAT_INITIALIZE_GAME,
                                       reinterpret_cast<void *>(&InitializeGame_h),
                                       reinterpret_cast<void **>(&g_origInitializeGame)};

SettingsLoad_t g_origSettingsLoad = nullptr;

void __fastcall SettingsLoad_h(int ecx, int file, void *arg) {
    g_origSettingsLoad(ecx, file, arg);
    const uint8_t *registry = Registry();
    for (int i = 0; i < Offsets::CHAT_TYPE_COUNT; ++i) {
        const auto *entry = reinterpret_cast<const char *>(registry + i * Offsets::CHAT_COLOR_STRIDE);
        Fire(entry, g_registry[i]);
    }
    const uint32_t count = ChannelCount();
    for (uint32_t i = 0; i < count; ++i) {
        const auto *entry =
            reinterpret_cast<const char *>(Channels() + i * Offsets::CHAT_COLOR_STRIDE);
        Fire(entry, ChannelFlag(i));
    }
}

const Game::HookAutoRegister _loadHook{Offsets::FUN_CHAT_SETTINGS_LOAD,
                                       reinterpret_cast<void *>(&SettingsLoad_h),
                                       reinterpret_cast<void **>(&g_origSettingsLoad)};

// The fifth COLORS word: Y sets the flag, N clears it, and anything else
// (a stock file has nothing there) leaves it.
void __cdecl ReadFlagWord(const char **cursor, const void *entry) {
    char word[0x100];
    word[0] = '\0';
    reinterpret_cast<SStrTokenize_t>(static_cast<uintptr_t>(Offsets::FUN_STORM_STR_TOKENIZE))(
        cursor, word, sizeof word,
        reinterpret_cast<const char *>(Offsets::VAR_CHAT_SETTINGS_WORD_DELIMS), nullptr);
    if (word[0] == 'Y' || word[0] == 'N')
        SetFlagAt(entry, word[0] == 'Y');
}

constexpr uintptr_t addrSStrToInt = Offsets::FUN_STORM_SSTR_TO_INT;
static_assert(Offsets::OFF_CHAT_SETTINGS_LOAD_LINE_CURSOR == -0x08,
              "ReadBlueThunk hard-codes [ebp - 8]");

// Stands in for the COLORS arm's blue `SStrToInt(word)` (stdcall, one
// argument). ESI is the entry and EBP the loader's frame; the blue value
// goes back in EAX for the arm's `mov [esi+0x42], al`.
__declspec(naked) void ReadBlueThunk() {
    __asm {
        push dword ptr [esp + 4]
        mov eax, addrSStrToInt
        call eax
        push eax
        push esi
        lea ecx, [ebp - 8]
        push ecx
        call ReadFlagWord
        add esp, 8
        pop eax
        ret 4
    }
}

// Stands in for both COLORS line writes; `name` is the entry itself.
int __cdecl WriteColorLine(char *dest, uint32_t maxChars, const char * /*format*/,
                           const char *name, int r, int g, int b) {
    return reinterpret_cast<SStrPrintf_t>(static_cast<uintptr_t>(Offsets::FUN_STORM_SSTR_PRINTF))(
        dest, maxChars, "%s %d %d %d %s\n", name, r, g, b, FlagAt(name) ? "Y" : "N");
}

void PatchSettingsFile() {
    Common::RedirectCall(Offsets::CALL_CHAT_SETTINGS_READ_COLOR_BLUE,
                         Offsets::FUN_STORM_SSTR_TO_INT,
                         reinterpret_cast<const void *>(&ReadBlueThunk));
    Common::RedirectCall(Offsets::CALL_CHAT_SETTINGS_WRITE_COLOR, Offsets::FUN_STORM_SSTR_PRINTF,
                         reinterpret_cast<const void *>(&WriteColorLine));
    Common::RedirectCall(Offsets::CALL_CHAT_SETTINGS_WRITE_CHANNEL_COLOR,
                         Offsets::FUN_STORM_SSTR_PRINTF,
                         reinterpret_cast<const void *>(&WriteColorLine));
}

// --- SetChatColorNameByClass ------------------------------------------------

// 4.3.4's loops: the first registry entry whose name matches, then the first
// extra channel entry whose name matches.
int __fastcall Script_SetChatColorNameByClass(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: SetChatColorNameByClass(chatType, colorNameByClass)");
        return 0;
    }
    const char *type = Game::Lua::ToString(L, 1);
    const bool on = Game::Lua::ToFlag(L, 2);
    const auto cmp = reinterpret_cast<SStrCmpI_t>(static_cast<uintptr_t>(Offsets::FUN_SSTR_CMP_I));
    bool found = false;

    const uint8_t *registry = Registry();
    for (int i = 0; i < Offsets::CHAT_TYPE_COUNT; ++i) {
        const auto *name = reinterpret_cast<const char *>(registry + i * Offsets::CHAT_COLOR_STRIDE);
        if (cmp(type, name, kMaxStr) == 0) {
            g_registry[i] = on;
            found = true;
            break;
        }
    }

    const uint32_t count = ChannelCount();
    for (uint32_t i = 0; i < count; ++i) {
        const auto *name = reinterpret_cast<const char *>(Channels() + i * Offsets::CHAT_COLOR_STRIDE);
        if (cmp(type, name, kMaxStr) == 0) {
            ChannelFlag(i) = on;
            found = true;
            break;
        }
    }

    if (found)
        Fire(type, on);
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("chatType", "cstring",
                   "A chat type such as \"GUILD\" or \"WHISPER\", or a channel such as \"CHANNEL1\"."),
    Game::Doc::Req("colorNameByClass", "bool"),
};
const Game::Doc::Function kSetDoc{
    "Sets whether sender names in a chat type show in their class colour, and saves the "
    "setting with the chat colours.",
    kArgs, {}, "ChatGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    PatchSettingsFile();
    Game::Lua::RegisterGlobalFunction("SetChatColorNameByClass", &Script_SetChatColorNameByClass,
                                      &kSetDoc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Chat::ColorNameByClass
