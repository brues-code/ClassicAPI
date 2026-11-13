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

// Lua 5.1 math-library additions that 1.12's Lua 5.0 is missing:
//
//   - `math.fmod(x, y)` — floating-point remainder of `x / y`.
//   - `math.modf(x)`    — split into integral + fractional parts.
//   - `math.huge`       — positive infinity constant.
//   - `math.cosh(x)`, `math.sinh(x)`, `math.tanh(x)` — hyperbolic functions.
//
// `math.fmod` is a pure rename: Lua 5.0 ships the exact same C function under
// the name `math.mod`; 5.1 renamed the Lua-facing entry to `math.fmod`. We
// register it as a direct alias of the engine's `math.mod` C function
// (`FUN_LUA_MATH_FMOD`) — no wrapper. `math.mod` stays available too, so code
// written for either name works. `modf` and `huge` are genuinely new in 5.1
// (not in 1.12's mathlib table), so we implement/define them here, as are the
// hyperbolic functions (5.1's math_cosh/sinh/tanh: luaL_checknumber, the C
// function, one number).

#include "Game.h"
#include "Offsets.h"

#include <cmath>

namespace BaseLib::MathLib {

namespace {

// `math.fmod` is 5.0's `math.mod` verbatim — register the engine function
// pointer directly rather than wrapping it.
const auto Script_math_fmod =
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_MATH_FMOD);

// `math.modf(x)` — returns `(integral, fractional)`, the integral part
// truncated toward zero and the fractional part carrying `x`'s sign
// (`modf(3.7) → 3, 0.7`; `modf(-3.7) → -3, -0.7`). Matches C `modf` / Lua 5.1.
int __fastcall Script_math_modf(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: math.modf(x)");
        return 0; // unreachable
    }
    double integral = 0.0;
    const double fractional = std::modf(Game::Lua::ToNumber(L, 1), &integral);
    Game::Lua::PushNumber(L, integral);
    Game::Lua::PushNumber(L, fractional);
    return 2;
}

int __fastcall Script_math_cosh(void *L) {
    Game::Lua::PushNumber(L, std::cosh(Game::Lua::CheckNumber(L, 1)));
    return 1;
}

int __fastcall Script_math_sinh(void *L) {
    Game::Lua::PushNumber(L, std::sinh(Game::Lua::CheckNumber(L, 1)));
    return 1;
}

int __fastcall Script_math_tanh(void *L) {
    Game::Lua::PushNumber(L, std::tanh(Game::Lua::CheckNumber(L, 1)));
    return 1;
}

const Game::Doc::Field kFmodArgs[] = {
    Game::Doc::Req("x", "number"),
    Game::Doc::Req("y", "number"),
};
const Game::Doc::Field kFmodRets[] = {
    Game::Doc::Req("remainder", "number", "Has the sign of x."),
};
const Game::Doc::Function kFmod{
    "Returns the remainder of x divided by y, with the quotient rounded toward zero.",
    kFmodArgs, kFmodRets};

const Game::Doc::Field kXArgs[] = {
    Game::Doc::Req("x", "number"),
};
const Game::Doc::Field kModfRets[] = {
    Game::Doc::Req("integral", "number", "The whole-number part, rounded toward zero."),
    Game::Doc::Req("fractional", "number", "The rest of x, with the sign of x."),
};
const Game::Doc::Function kModf{"Splits a number into its integral and fractional parts.",
                                kXArgs, kModfRets};

const Game::Doc::Field kResultRets[] = {
    Game::Doc::Req("result", "number"),
};
const Game::Doc::Function kCosh{"Returns the hyperbolic cosine of x.", kXArgs, kResultRets};
const Game::Doc::Function kSinh{"Returns the hyperbolic sine of x.", kXArgs, kResultRets};
const Game::Doc::Function kTanh{"Returns the hyperbolic tangent of x.", kXArgs, kResultRets};

void RegisterFns() {
    Game::Lua::RegisterTableFunction("math", "fmod", Script_math_fmod, &kFmod);
    Game::Lua::RegisterTableFunction("math", "modf", &Script_math_modf, &kModf);
    Game::Lua::RegisterTableFunction("math", "cosh", &Script_math_cosh, &kCosh);
    Game::Lua::RegisterTableFunction("math", "sinh", &Script_math_sinh, &kSinh);
    Game::Lua::RegisterTableFunction("math", "tanh", &Script_math_tanh, &kTanh);

    // `math.huge` — a numeric constant, not a function. Fetch the (now
    // guaranteed-present) `math` table and set the field directly.
    void *L = Game::Lua::State();
    if (L != nullptr) {
        Game::Lua::PushString(L, "math");
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [math]
        if (Game::Lua::Type(L, -1) == 5 /* LUA_TTABLE */) {
            Game::Lua::SetFieldNumber(L, "huge", HUGE_VAL); // math.huge = inf; [math]
            Game::Lua::SetTop(L, -2);                        // pop [math]
        } else {
            Game::Lua::SetTop(L, -2); // pop whatever GetTable returned
        }
    }
}

// Both states are Lua 5.0 and equally missing this; RegisterTableFunction
// targets whichever state is active in each registration hook.
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace BaseLib::MathLib
