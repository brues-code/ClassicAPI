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

// A self-registering callback list: the shape behind every `AutoSubscribe` /
// `*AutoRegister` / `AutoResolver` here. A module declares a file-scope
//
//     static const Some::AutoSubscribe _sub{&Callback};
//
// and the constructor pushes it onto the list at static-init time, before
// DllMain runs. The owner walks it with
//
//     for (auto *node = AutoSubscribe::head; node != nullptr; node = node->next)
//         node->fn(...);
//
// and keeps its own dispatch rule (all of them, first match, ...). `head` is
// constant-initialized to null before any dynamic initializer runs, so the
// order the TUs initialize in never matters. The order of the nodes is LIFO of
// that order, which is unspecified: no callback may depend on another's.
//
// Declare a list as `using AutoSubscribe = AutoList<struct AutoSubscribeTag,
// Callback>;`. The tag (declared in the enclosing namespace by that use) keeps
// two lists with the same callback type apart.
template <class Tag, class Fn> struct AutoList {
    explicit AutoList(Fn f) : fn(f), next(head) { head = this; }
    AutoList(const AutoList &) = delete;
    AutoList &operator=(const AutoList &) = delete;

    Fn fn;
    AutoList *next;
    static inline AutoList *head = nullptr;
};
