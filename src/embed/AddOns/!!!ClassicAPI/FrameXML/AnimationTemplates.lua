-- Backport of one mixin from retail's Blizzard_SharedXML/AnimationTemplates.lua.

-- Anim group whose parent is hidden or shown based on script calls defined in the template xml being used
VisibleWhilePlayingAnimGroupMixin = {}

function VisibleWhilePlayingAnimGroupMixin:Show()
	self:GetParent():Show();
end

function VisibleWhilePlayingAnimGroupMixin:Hide()
	self:GetParent():Hide();
end
