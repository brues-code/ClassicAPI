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

// Text-to-speech — a Windows SAPI backend behind modern WoW's C_VoiceChat /
// C_TTSSettings Lua surface. Ported from awesome_wotlk's VoiceChat.cpp (the
// 3.3.5 implementation); the SAPI core is client-version-agnostic, so only
// the three integration seams are ClassicAPI-native:
//   - namespaces  → Game::Lua::RegisterTableFunction
//   - events      → Event::Custom (AutoReserve + Fire)
//   - settings    → CVar::Factory (ttsVoice / ttsSpeed / ttsVolume, persisted
//                   to Config.wtf, clamped via change callbacks)
//
// Lua surface:
//   C_VoiceChat
//     GetTtsVoices()            -> { {voiceID=, name=}, ... }
//     GetRemoteTtsVoices()      -> same (vanilla has no real voice chat)
//     SpeakText(voiceID, text[, destination[, rate[, volume]]])
//     StopSpeakingText()
//   C_TTSSettings
//     GetSpeechRate() / GetSpeechVolume() / GetSpeechVoiceID()
//     GetVoiceOptionName()
//     SetDefaultSettings()
//     SetSpeechRate(v) / SetSpeechVolume(v)
//     SetVoiceOption(id) / SetVoiceOptionByName(name)
//     RefreshVoices()
//
// Events: VOICE_CHAT_TTS_PLAYBACK_STARTED/FINISHED/FAILED,
//         VOICE_CHAT_TTS_SPEAK_TEXT_UPDATE (reserved, unused),
//         VOICE_CHAT_TTS_VOICES_UPDATE.
//
// Threading: SAPI is created on the main (game) thread under an STA, and
// SetNotifyCallbackFunction marshals the notify back to that thread's message
// pump (WoW pumps messages every frame), so OnSpeechNotify runs on the main
// thread and may touch the Lua event dispatcher safely.
//
// Coexistence: the same TTS surface also ships as the standalone VanillaTTS
// DLL (which adds a pluggable backend — software synth under Wine). When
// that DLL is injected alongside us, this module stands down entirely — see
// StandaloneTtsPresent below.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"

#include <initguid.h> // instantiate the SAPI GUIDs in this TU (no sapi.lib)
#include <sapi.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace VoiceChat::Tts {

namespace {

// ---------------------------------------------------------------------------
// Standalone-DLL coexistence
// ---------------------------------------------------------------------------
// The dedicated VanillaTTS DLL provides this exact Lua surface (same
// C_VoiceChat / C_TTSSettings functions, same VOICE_CHAT_TTS_* events, same
// ttsVoice/ttsSpeed/ttsVolume cvars) with a more capable backend. If both
// registered, the damage wouldn't be limited to "last one wins":
//   - Each DLL claims its OWN event-table slot for the same event name, so
//     the table ends up with duplicate-name entries. frame:RegisterEvent
//     binds listeners to whichever slot the engine's scan finds first, while
//     the other DLL fires the other slot — events silently lost depending on
//     load order.
//   - The cvar registrar dedups by name but REPLACES the change callback on
//     re-register, so one DLL's clamp callbacks (and userData) go dead.
//   - Both would init COM/SAPI and answer SpeakText → double playback risk.
// So when VanillaTTS.dll is in the process, this module registers nothing —
// no Lua functions, no cvars, no event reservations, no SAPI init.
//
// The check runs at RegisterLuaFunctions time (first login), which is
// load-order-independent: VanillaFixes loads every DLL at process attach,
// long before FUN_LOAD_SCRIPT_FUNCTIONS first fires. Latched once — modules
// can't appear or vanish mid-session.
bool StandaloneTtsPresent() {
    static const bool present = GetModuleHandleA("VanillaTTS.dll") != nullptr;
    return present;
}

// ---------------------------------------------------------------------------
// Event names — reserved lazily in RegisterLuaFunctions (NOT via file-scope
// AutoReserve statics, which would run unconditionally at DLL load and claim
// slots even when we're standing down for VanillaTTS).
// ---------------------------------------------------------------------------
constexpr const char *kEvtPlaybackFailed   = "VOICE_CHAT_TTS_PLAYBACK_FAILED";
constexpr const char *kEvtPlaybackFinished = "VOICE_CHAT_TTS_PLAYBACK_FINISHED";
constexpr const char *kEvtPlaybackStarted  = "VOICE_CHAT_TTS_PLAYBACK_STARTED";
constexpr const char *kEvtSpeakTextUpdate  = "VOICE_CHAT_TTS_SPEAK_TEXT_UPDATE";
constexpr const char *kEvtVoicesUpdate     = "VOICE_CHAT_TTS_VOICES_UPDATE";

// The lazily-created reservations, set by RegisterLuaFunctions. Null while
// standing down for VanillaTTS — but the fire helpers only run from our own
// registered surface, so they never see null in that mode; SlotOf's guard is
// belt-and-suspenders (`Fire(-1)` no-ops).
const Event::Custom::AutoReserve *g_evtPlaybackFailed = nullptr;
const Event::Custom::AutoReserve *g_evtPlaybackFinished = nullptr;
const Event::Custom::AutoReserve *g_evtPlaybackStarted = nullptr;
const Event::Custom::AutoReserve *g_evtVoicesUpdate = nullptr;

int SlotOf(const Event::Custom::AutoReserve *r) {
    return r != nullptr ? r->Slot() : -1;
}

// ---------------------------------------------------------------------------
// CVar names + handles (registered in RegisterLuaFunctions)
// ---------------------------------------------------------------------------
constexpr const char *kCVarVoice  = "ttsVoice";
constexpr const char *kCVarSpeed  = "ttsSpeed";
constexpr const char *kCVarVolume = "ttsVolume";

CVar::Factory::Handle g_cvarVoice  = nullptr;
CVar::Factory::Handle g_cvarSpeed  = nullptr;
CVar::Factory::Handle g_cvarVolume = nullptr;

// Destination constants (kept for API parity; no special handling — SAPI
// queues FIFO either way).
enum : int {
    DEST_LOCAL_PLAYBACK        = 1,
    DEST_QUEUED_LOCAL_PLAYBACK = 4,
};
int ClampDestination(int d) {
    return d == DEST_QUEUED_LOCAL_PLAYBACK ? DEST_QUEUED_LOCAL_PLAYBACK
                                           : DEST_LOCAL_PLAYBACK;
}

// ---------------------------------------------------------------------------
// SAPI state
// ---------------------------------------------------------------------------
ISpVoice *g_pVoice = nullptr;
bool g_comInit = false;

int g_nextUtteranceID = 1;
int NextUtteranceID() {
    if (g_nextUtteranceID >= 0x7fffffff)
        g_nextUtteranceID = 1;
    return g_nextUtteranceID++;
}

struct UtteranceMeta {
    int id;
    int destination;
    bool startedEmitted;
};
std::mutex g_streamMx;
std::unordered_map<ULONG, UtteranceMeta> g_streamMap;

struct VoiceEntry {
    int voiceID;
    std::wstring name;
};
std::vector<VoiceEntry> g_cachedVoices;

// ---------------------------------------------------------------------------
// UTF-8 / wide conversion
// ---------------------------------------------------------------------------
std::string WideToUtf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1)
        return {};
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], need - 1, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const char *s) {
    if (s == nullptr || *s == '\0')
        return {};
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (need <= 1)
        return {};
    std::wstring out(static_cast<size_t>(need - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], need);
    return out;
}

// ---------------------------------------------------------------------------
// Event fire helpers (engine dispatcher via Event::Custom)
// ---------------------------------------------------------------------------
void FireVoicesUpdate() {
    Event::Custom::Fire(SlotOf(g_evtVoicesUpdate), "");
}
void FirePlaybackStarted(int numConsumers, int utteranceID, int durationMS, int dest) {
    Event::Custom::Fire(SlotOf(g_evtPlaybackStarted), "%d%d%d%d",
                        numConsumers, utteranceID, durationMS, dest);
}
void FirePlaybackFinished(int numConsumers, int utteranceID, int dest) {
    Event::Custom::Fire(SlotOf(g_evtPlaybackFinished), "%d%d%d",
                        numConsumers, utteranceID, dest);
}
void FirePlaybackFailed(const char *status, int utteranceID, int dest) {
    Event::Custom::Fire(SlotOf(g_evtPlaybackFailed), "%s%d%d",
                        status, utteranceID, dest);
}

// ---------------------------------------------------------------------------
// COM / SAPI lifecycle
// ---------------------------------------------------------------------------
void InitCom() {
    if (g_comInit)
        return;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_FALSE = already initialized on this thread; treat as success.
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE)
        g_comInit = true;
}

void __stdcall OnSpeechNotify(WPARAM, LPARAM) {
    if (g_pVoice == nullptr)
        return;
    SPEVENT ev = {};
    ULONG fetched = 0;
    while (SUCCEEDED(g_pVoice->GetEvents(1, &ev, &fetched)) && fetched == 1) {
        if (ev.eEventId == SPEI_START_INPUT_STREAM) {
            std::lock_guard<std::mutex> lock(g_streamMx);
            auto it = g_streamMap.find(ev.ulStreamNum);
            if (it != g_streamMap.end() && !it->second.startedEmitted) {
                FirePlaybackStarted(1, it->second.id, /*durationMS*/ 0,
                                    it->second.destination);
                it->second.startedEmitted = true;
            }
        } else if (ev.eEventId == SPEI_END_INPUT_STREAM) {
            UtteranceMeta meta{0, DEST_LOCAL_PLAYBACK, false};
            {
                std::lock_guard<std::mutex> lock(g_streamMx);
                auto it = g_streamMap.find(ev.ulStreamNum);
                if (it != g_streamMap.end()) {
                    meta = it->second;
                    g_streamMap.erase(it);
                }
            }
            FirePlaybackFinished(1, meta.id, meta.destination);
        }
        // Only START/END stream events are requested; neither carries an
        // allocated lParam, so no SpClearEvent (sphelper) is needed.
        ev = SPEVENT{};
    }
}

void InitVoice() {
    if (g_pVoice != nullptr)
        return;
    InitCom();
    HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                  IID_ISpVoice, reinterpret_cast<void **>(&g_pVoice));
    if (FAILED(hr) || g_pVoice == nullptr) {
        g_pVoice = nullptr;
        return;
    }
    g_pVoice->SetNotifyCallbackFunction(&OnSpeechNotify, 0, 0);
    const ULONGLONG interest =
        SPFEI(SPEI_START_INPUT_STREAM) | SPFEI(SPEI_END_INPUT_STREAM);
    g_pVoice->SetInterest(interest, interest);
}

// ---------------------------------------------------------------------------
// Voice enumeration (CLSID_SpObjectTokenCategory; no sphelper)
// ---------------------------------------------------------------------------
std::vector<VoiceEntry> EnumerateVoices() {
    InitCom();
    std::vector<VoiceEntry> voices;

    ISpObjectTokenCategory *cat = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                IID_ISpObjectTokenCategory,
                                reinterpret_cast<void **>(&cat))) ||
        cat == nullptr)
        return voices;

    if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))) {
        IEnumSpObjectTokens *en = nullptr;
        if (SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &en)) && en != nullptr) {
            ISpObjectToken *tok = nullptr;
            ULONG fetched = 0;
            int index = 0;
            while (en->Next(1, &tok, &fetched) == S_OK && fetched == 1) {
                LPWSTR desc = nullptr;
                if (SUCCEEDED(tok->GetStringValue(nullptr, &desc)) && desc != nullptr) {
                    voices.push_back({index, desc});
                    CoTaskMemFree(desc);
                }
                tok->Release();
                ++index;
            }
            en->Release();
        }
    }
    cat->Release();
    return voices;
}

int VoiceCount() {
    return static_cast<int>(EnumerateVoices().size());
}

void RefreshVoices() {
    auto fresh = EnumerateVoices();
    bool changed = fresh.size() != g_cachedVoices.size();
    if (!changed) {
        for (size_t i = 0; i < fresh.size(); ++i) {
            if (fresh[i].name != g_cachedVoices[i].name) {
                changed = true;
                break;
            }
        }
    }
    g_cachedVoices = std::move(fresh);
    if (changed)
        FireVoicesUpdate();
}

// Select the voice at enumeration index `voiceID` on g_pVoice.
bool SelectVoice(int voiceID) {
    ISpObjectTokenCategory *cat = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                IID_ISpObjectTokenCategory,
                                reinterpret_cast<void **>(&cat))) ||
        cat == nullptr)
        return false;

    bool set = false;
    if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))) {
        IEnumSpObjectTokens *en = nullptr;
        if (SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &en)) && en != nullptr) {
            ISpObjectToken *tok = nullptr;
            ULONG fetched = 0;
            int index = 0;
            while (en->Next(1, &tok, &fetched) == S_OK && fetched == 1) {
                if (index == voiceID) {
                    set = SUCCEEDED(g_pVoice->SetVoice(tok));
                    tok->Release();
                    break;
                }
                tok->Release();
                ++index;
            }
            en->Release();
        }
    }
    cat->Release();
    return set;
}

// ---------------------------------------------------------------------------
// Speak / stop
// ---------------------------------------------------------------------------
void SpeakText(int voiceID, const std::wstring &text, int destination, int rate,
               int volume) {
    const int utteranceID = NextUtteranceID();
    const int dest = ClampDestination(destination);

    InitVoice();
    if (g_pVoice == nullptr) {
        FirePlaybackFailed("EngineAllocationFailed", utteranceID, dest);
        return;
    }
    if (!SelectVoice(voiceID)) {
        FirePlaybackFailed("InternalError", utteranceID, dest);
        return;
    }

    g_pVoice->SetRate(std::clamp(rate, -10, 10));
    g_pVoice->SetVolume(static_cast<USHORT>(std::clamp(volume, 0, 100)));

    ULONG streamNum = 0;
    if (FAILED(g_pVoice->Speak(text.c_str(), SPF_ASYNC, &streamNum))) {
        FirePlaybackFailed("InternalError", utteranceID, dest);
        return;
    }
    std::lock_guard<std::mutex> lock(g_streamMx);
    g_streamMap[streamNum] = UtteranceMeta{utteranceID, dest, false};
}

void StopAll() {
    InitVoice();
    if (g_pVoice == nullptr)
        return;
    g_pVoice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
    std::lock_guard<std::mutex> lock(g_streamMx);
    g_streamMap.clear();
}

// ---------------------------------------------------------------------------
// Lua helpers
// ---------------------------------------------------------------------------
// Push a { {voiceID=, name=}, ... } array built from g_cachedVoices.
void PushVoicesTable(void *L) {
    Game::Lua::NewTable(L); // outer array at top
    for (size_t i = 0; i < g_cachedVoices.size(); ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1)); // 1-based array key
        Game::Lua::NewTable(L);                               // sub-table
        Game::Lua::SetFieldNumber(L, "voiceID", g_cachedVoices[i].voiceID);
        Game::Lua::SetFieldString(L, "name",
                                  WideToUtf8(g_cachedVoices[i].name).c_str());
        // stack: [outer, key, sub] → outer[key] = sub
        Game::Lua::RawSet(L, -3);
    }
}

int OptInt(void *L, int idx, int fallback) {
    return Game::Lua::IsNumber(L, idx)
               ? static_cast<int>(Game::Lua::ToNumber(L, idx))
               : fallback;
}

// ---------------------------------------------------------------------------
// C_VoiceChat
// ---------------------------------------------------------------------------
int __fastcall Script_GetTtsVoices(void *L) {
    RefreshVoices();
    PushVoicesTable(L);
    return 1;
}

int __fastcall Script_GetRemoteTtsVoices(void *L) {
    RefreshVoices();
    PushVoicesTable(L);
    return 1;
}

int __fastcall Script_SpeakText(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: C_VoiceChat.SpeakText(voiceID, text "
                            "[, destination, rate, volume])");
        return 0;
    }
    const int voiceID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const std::wstring text = Utf8ToWide(Game::Lua::ToString(L, 2));
    const int dest = OptInt(L, 3, DEST_LOCAL_PLAYBACK);
    const int rate = OptInt(L, 4, 0);
    const int volume = OptInt(L, 5, 100);
    SpeakText(voiceID, text, dest, rate, volume);
    return 0;
}

int __fastcall Script_StopSpeakingText(void *) {
    StopAll();
    return 0;
}

// ---------------------------------------------------------------------------
// C_TTSSettings
// ---------------------------------------------------------------------------
int __fastcall Script_GetSpeechRate(void *L) {
    Game::Lua::PushNumber(L, CVar::Factory::GetInt(g_cvarSpeed, 0));
    return 1;
}
int __fastcall Script_GetSpeechVolume(void *L) {
    Game::Lua::PushNumber(L, CVar::Factory::GetInt(g_cvarVolume, 100));
    return 1;
}
int __fastcall Script_GetSpeechVoiceID(void *L) {
    Game::Lua::PushNumber(L, CVar::Factory::GetInt(g_cvarVoice, 0));
    return 1;
}
int __fastcall Script_GetVoiceOptionName(void *L) {
    const int id = CVar::Factory::GetInt(g_cvarVoice, 0);
    auto voices = EnumerateVoices();
    if (id >= 0 && id < static_cast<int>(voices.size()))
        Game::Lua::PushString(L, WideToUtf8(voices[id].name).c_str());
    else
        Game::Lua::PushString(L, "");
    return 1;
}

int __fastcall Script_SetSpeechRate(void *L) {
    if (Game::Lua::IsNumber(L, 1))
        CVar::Factory::SetInt(g_cvarSpeed, static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}
int __fastcall Script_SetSpeechVolume(void *L) {
    if (Game::Lua::IsNumber(L, 1))
        CVar::Factory::SetInt(g_cvarVolume, static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}
int __fastcall Script_SetVoiceOption(void *L) {
    if (Game::Lua::IsNumber(L, 1))
        CVar::Factory::SetInt(g_cvarVoice, static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}
int __fastcall Script_SetVoiceOptionByName(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const std::wstring want = Utf8ToWide(Game::Lua::ToString(L, 1));
    auto voices = EnumerateVoices();
    for (const auto &v : voices) {
        if (v.name.size() == want.size()) {
            bool eq = true;
            for (size_t i = 0; i < want.size(); ++i) {
                if (towlower(v.name[i]) != towlower(want[i])) {
                    eq = false;
                    break;
                }
            }
            if (eq) {
                CVar::Factory::SetInt(g_cvarVoice, v.voiceID);
                break;
            }
        }
    }
    return 0;
}
int __fastcall Script_SetDefaultSettings(void *) {
    const int maxVoice = VoiceCount();
    CVar::Factory::SetInt(g_cvarVoice, maxVoice > 1 ? 1 : 0);
    CVar::Factory::SetInt(g_cvarSpeed, 0);
    CVar::Factory::SetInt(g_cvarVolume, 100);
    FireVoicesUpdate();
    return 0;
}
int __fastcall Script_RefreshVoices(void *) {
    RefreshVoices();
    return 0;
}

// ---------------------------------------------------------------------------
// CVar clamp callbacks (re-entrant SetString to clamp; reject out-of-range)
// ---------------------------------------------------------------------------
int __fastcall ClampRange(CVar::Factory::Handle cvar, const char *, const char *next,
                          int lo, int hi) {
    if (next == nullptr)
        return 1;
    const int v = std::atoi(next);
    const int c = std::clamp(v, lo, hi);
    if (c != v) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", c);
        CVar::Factory::SetString(cvar, buf);
        return 0;
    }
    return 1;
}
int __fastcall OnSpeedChanged(CVar::Factory::Handle c, const char *p, const char *n, void *) {
    return ClampRange(c, p, n, -10, 10);
}
int __fastcall OnVolumeChanged(CVar::Factory::Handle c, const char *p, const char *n, void *) {
    return ClampRange(c, p, n, 0, 100);
}
int __fastcall OnVoiceChanged(CVar::Factory::Handle c, const char *p, const char *n, void *) {
    const int maxIdx = VoiceCount() - 1;
    return ClampRange(c, p, n, 0, maxIdx < 0 ? 0 : maxIdx);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void RegisterLuaFunctions() {
    if (StandaloneTtsPresent())
        return; // VanillaTTS owns the TTS surface this session — see above.

    // Reserve the events on first (real) registration. AutoReserve only
    // appends to Event::Custom's reservation list — the actual slot claims
    // happen later via the usual RetryClaims walk — so reserving here instead
    // of at static-init time changes nothing but the conditionality.
    static const Event::Custom::AutoReserve _r1{kEvtPlaybackFailed};
    static const Event::Custom::AutoReserve _r2{kEvtPlaybackFinished};
    static const Event::Custom::AutoReserve _r3{kEvtPlaybackStarted};
    static const Event::Custom::AutoReserve _r4{kEvtSpeakTextUpdate};
    static const Event::Custom::AutoReserve _r5{kEvtVoicesUpdate};
    g_evtPlaybackFailed = &_r1;
    g_evtPlaybackFinished = &_r2;
    g_evtPlaybackStarted = &_r3;
    g_evtVoicesUpdate = &_r5;

    // Retail has no cvars by these names, so there is no help line to copy.
    constexpr int kCategory = Offsets::CVAR_CATEGORY_SOUND;
    g_cvarVoice  = CVar::Factory::Register(kCVarVoice,  nullptr, kCategory, "0",   &OnVoiceChanged);
    g_cvarSpeed  = CVar::Factory::Register(kCVarSpeed,  nullptr, kCategory, "0",   &OnSpeedChanged);
    g_cvarVolume = CVar::Factory::Register(kCVarVolume, nullptr, kCategory, "100", &OnVolumeChanged);

    Game::Lua::RegisterTableFunction("C_VoiceChat", "GetTtsVoices", &Script_GetTtsVoices);
    Game::Lua::RegisterTableFunction("C_VoiceChat", "GetRemoteTtsVoices", &Script_GetRemoteTtsVoices);
    Game::Lua::RegisterTableFunction("C_VoiceChat", "SpeakText", &Script_SpeakText);
    Game::Lua::RegisterTableFunction("C_VoiceChat", "StopSpeakingText", &Script_StopSpeakingText);

    Game::Lua::RegisterTableFunction("C_TTSSettings", "GetSpeechRate", &Script_GetSpeechRate);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "GetSpeechVolume", &Script_GetSpeechVolume);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "GetSpeechVoiceID", &Script_GetSpeechVoiceID);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "GetVoiceOptionName", &Script_GetVoiceOptionName);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "SetDefaultSettings", &Script_SetDefaultSettings);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "SetSpeechRate", &Script_SetSpeechRate);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "SetSpeechVolume", &Script_SetSpeechVolume);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "SetVoiceOption", &Script_SetVoiceOption);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "SetVoiceOptionByName", &Script_SetVoiceOptionByName);
    Game::Lua::RegisterTableFunction("C_TTSSettings", "RefreshVoices", &Script_RefreshVoices);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace VoiceChat::Tts
