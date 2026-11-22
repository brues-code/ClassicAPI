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

// A model instance's desaturation: its mesh batches, and those of the models
// attached under it, draw with their colour moved toward grey by `amount`.
namespace Model::Desaturation {

// `amount` 0..1 (clamped); 0 removes the model's entry. The caller clears the
// entry before the model is released.
void Set(void *model, float amount);

} // namespace Model::Desaturation
