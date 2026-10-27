-- `Mixin` and `CreateFromMixins` are engine (DLL) natives — see
-- src/baselib/Mixin.cpp — so they exist whenever ClassicAPI is injected,
-- even with this addon disabled, mirroring retail where they live in
-- TableUtil as C functions. Only the composite helper (which calls a Lua
-- `:Init` method) stays here, like retail's Mixin.lua.
--
-- Lua 5.0 note: varargs arrive in the implicit `arg` table (`arg.n` count),
-- so we forward them with unpack(arg) rather than 5.1's `...`.

function CreateAndInitFromMixin(mixin, ...)
    local object = CreateFromMixins(mixin)
    object:Init(unpack(arg))
    return object
end
