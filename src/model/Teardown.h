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

// Model frame teardown. A Model frame's destructor (FUN_SIMPLEMODEL_DESTROY)
// releases its M2 scene, and the scene frees every model in it, whatever
// references are still out. Anything that holds an engine object tied to a
// frame's model (a compositor, an effect model) releases it here, the way the
// engine's own frames free theirs in their destructors. The UI teardown
// (CGGameUI::Shutdown, on /reload and logout) destroys every frame long
// before a ReloadAutoRegister cleanup runs, so a reload cleanup is too late
// for this.
//
// Declare a file-scope `const Model::Teardown::AutoSubscribe` in the module's
// TU; the callback runs with the frame and its models still intact.

namespace Model::Teardown {

using Callback = void (*)(void *frame);

struct AutoSubscribe {
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Model::Teardown
