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

// Listeners run when a font string is destroyed, before its destructor body
// (CSimpleFontString::~CSimpleFontString). The block then goes back to the
// region pool and can come back as another font string, so a module that keeps
// a font string pointer must drop it here. Same pattern as Frame::Destruction.
namespace FontString::Destruction {

using Listener = void (*)(void *fontString);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace FontString::Destruction
