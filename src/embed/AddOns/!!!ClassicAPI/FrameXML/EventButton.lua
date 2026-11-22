-- Backport of retail's Blizzard_SharedXML/Shared/Frame/EventButton.lua.
--
-- Implementation differences from the retail source:
--   - 1.12's Button:IsEnabled returns 1 or 0, and 0 is true in Lua, so the
--     sound and callback gates compare it with 1.

EventButtonMixin = CreateFromMixins(CallbackRegistryMixin);

EventButtonMixin:GenerateCallbackEvents(
	{
		"OnMouseUp",
		"OnMouseDown",
		"OnClick",
		"OnEnter",
		"OnLeave",
		"OnSizeChanged",
	}
);

local function PlaySoundKit(button, soundKitID)
	if soundKitID and button:IsEnabled() == 1 then
		PlaySound(soundKitID);
	end
end

function EventButtonMixin:OnLoad_Intrinsic()
	CallbackRegistryMixin.OnLoad(self);
end

function EventButtonMixin:OnMouseUp_Intrinsic(buttonName, upInside)
	self:TriggerEvent("OnMouseUp", buttonName, upInside);
	PlaySoundKit(self, self.mouseUpSoundKitID);
end

function EventButtonMixin:OnMouseDown_Intrinsic(buttonName)
	if self:IsEnabled() == 1 then
		self:TriggerEvent("OnMouseDown", buttonName);
		PlaySoundKit(self, self.mouseDownSoundKitID);
	end
end

function EventButtonMixin:OnClick_Intrinsic(buttonName, down)
	if self:IsEnabled() == 1 then
		self:TriggerEvent("OnClick", buttonName, down);
		PlaySoundKit(self, self.clickSoundKitID);
	end
end

function EventButtonMixin:OnEnter_Intrinsic()
	if self:IsEnabled() == 1 then
		self:TriggerEvent("OnEnter");
	end
end

function EventButtonMixin:OnLeave_Intrinsic()
	self:TriggerEvent("OnLeave");
end

function EventButtonMixin:OnSizeChanged_Intrinsic(width, height)
	self:TriggerEvent("OnSizeChanged", width, height);
end