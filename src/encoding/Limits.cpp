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

// `Constants.EncodingLimits`: the decompressed-size cap of
// `C_EncodingUtil.DecompressString` and the nesting cap of the JSON and CBOR
// functions.

#include "Game.h"
#include "encoding/Args.h"

namespace Encoding::Limits {

namespace {

void Register() {
    static const Game::Lua::EnumIntegerEntry kLimits[] = {
        {"EncodingDecompressSizeLimit", static_cast<int>(Args::kDecompressSizeLimit)},
        {"EncodingStackSizeLimit", Args::kStackSizeLimit},
    };
    Game::Lua::RegisterConstants("EncodingLimits", kLimits, "EncodingUtil");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Encoding::Limits
