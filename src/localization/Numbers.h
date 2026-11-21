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

#pragma once

#include <string>
#include <vector>

// What the number abbreviations share with C_StringUtil's
// AbbreviatedNumberFormatter: the client's default breakpoints (3.80.2 builds
// one list for every user, 0x1401AB2A0) and the global-string lookup.
namespace Localization::Numbers {

struct AbbreviationBreakpoint {
    double breakpoint = 0.0;
    std::string abbreviation;
    double significandDivisor = 0.0;
    double fractionDivisor = 0.0;
    bool abbreviationIsGlobal = true;
};

// C_StringUtil.GetDefaultAbbreviationBreakpoints() for the client's locale
// (the Asian list for koKR, zhCN and zhTW), largest first.
std::vector<AbbreviationBreakpoint> DefaultAbbreviationBreakpoints();

// `_G[name]` when it is a string, else "".
std::string GlobalString(void *L, const char *name);

} // namespace Localization::Numbers
