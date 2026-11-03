// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

namespace Model::DisplayInfo {

// The model's M2 and any pending humanoid appearance composition are ready.
bool IsPortraitAppearanceReady(void *pane);

// The reload teardown destroys every Model frame (and with it the model
// instances our pending character-dress jobs point at). Drop all jobs and
// destroy their engine compositors before the engine starts tearing down.
void PrepareForReload();

} // namespace Model::DisplayInfo
