-- ClassicAPI's own Settings control: a number box, for the settings that take
-- any number (a range in yards, a size, a time). Retail's Settings has no such
-- control, and a slider would put a limit on these values that the settings do
-- not have. It is built the way retail builds its controls: on
-- SettingsListElementTemplate and SettingsControlMixin, so it has the row's
-- label, tooltip, indent, parent predicates and search like every other
-- control.
--
-- The box writes when Enter is pressed or it loses the focus. Escape puts the
-- current value back. Text that is not a number is not written.

ClassicAPISettingsNumberBoxControlMixin = CreateFromMixins(SettingsControlMixin);

function ClassicAPISettingsNumberBoxControlMixin:OnLoad()
	SettingsControlMixin.OnLoad(self);

	Mixin(self.EditBox, DefaultTooltipMixin);
	DefaultTooltipMixin.OnLoad(self.EditBox);

	self.EditBox:SetScript("OnEnterPressed", function(editBox)
		editBox:ClearFocus();
	end);
	self.EditBox:SetScript("OnEscapePressed", function(editBox)
		self:SetValue(self:GetSetting():GetValue());
		editBox:ClearFocus();
	end);
	self.EditBox:SetScript("OnEditFocusGained", function(editBox)
		editBox:HighlightText();
	end);
	self.EditBox:SetScript("OnEditFocusLost", function(editBox)
		editBox:HighlightText(0, 0);
		local value = tonumber(editBox:GetText());
		local setting = self:GetSetting();
		if value and value ~= setting:GetValue() then
			setting:SetValue(value);
		end
		self:SetValue(setting:GetValue());
	end);
end

function ClassicAPISettingsNumberBoxControlMixin:Init(initializer)
	SettingsControlMixin.Init(self, initializer);

	self:SetValue(self:GetSetting():GetValue());
	self.EditBox:SetTooltipFunc(GenerateClosure(Settings.InitTooltip, initializer:GetName(), initializer:GetTooltip()));

	self:EvaluateState();
end

function ClassicAPISettingsNumberBoxControlMixin:SetValue(value)
	self.EditBox:SetText(value ~= nil and tostring(value) or "");
end

function ClassicAPISettingsNumberBoxControlMixin:EvaluateState()
	SettingsListElementMixin.EvaluateState(self);
	local enabled = self:IsEnabled();
	if enabled then
		self.EditBox:Enable();
	else
		self.EditBox:Disable();
	end
	self:DisplayEnabled(enabled);
end

function ClassicAPISettingsNumberBoxControlMixin:Release()
	self.EditBox:ClearFocus();
	SettingsControlMixin.Release(self);
end

