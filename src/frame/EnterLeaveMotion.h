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

// OnEnter / OnLeave `motion` (EnterLeaveMotion.cpp).
namespace Frame::EnterLeaveMotion {

// For its lifetime, `frame`'s OnEnter / OnLeave runs are offered `motion`
// instead of the focus pass's answer: a scripted enter or leave
// (FocusEnter / FocusExit). Nestable; restores the previous offer.
class Offer {
  public:
    Offer(void *frame, bool motion);
    ~Offer();
    Offer(const Offer &) = delete;
    Offer &operator=(const Offer &) = delete;

  private:
    void *m_frame;
    bool m_motion;
};

} // namespace Frame::EnterLeaveMotion
