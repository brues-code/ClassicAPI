-- Backport of retail's Blizzard_SharedXMLBase/ButtonStateBehavior.lua.
--
-- Implementation differences from the retail source:
--   - 1.12's Button:IsEnabled returns 1 or 0, and 0 is true in Lua, so the
--     enter, leave and mouse gates compare it with 1.

ButtonStateBehaviorMixin = {};

function ButtonStateBehaviorMixin:OnLoad()
	self:OnButtonStateChanged();
end

-- Will displace regions on mouse down and on mouse up.
function ButtonStateBehaviorMixin:SetDisplacedRegions(x, y, ...)
	self.displacedRegions = {...};
	self.displaceX = x;
	self.displaceY = y;
end

-- Will saturate/desaturate the entire button hierarchy when enabled and disabled.
function ButtonStateBehaviorMixin:DesaturateIfDisabled()
	self.desaturateIfDisabled = true;
end

function ButtonStateBehaviorMixin:OnButtonStateChanged()
	-- Derive and configure your button to the correct state.
end

function ButtonStateBehaviorMixin:IsDownOver()
	return self.down and self.over;
end

function ButtonStateBehaviorMixin:IsDown()
	return self.down;
end

function ButtonStateBehaviorMixin:IsOver()
	return self.over;
end

function ButtonStateBehaviorMixin:OnEnter()
	if self:IsEnabled() == 1 then
		self.over = true;
		self:OnButtonStateChanged();
		return true;
	end
	return false;
end

function ButtonStateBehaviorMixin:OnLeave()
	if self:IsEnabled() == 1 then
		self.over = nil;
		self:OnButtonStateChanged();
		return true;
	end
	return false;
end

function ButtonStateBehaviorMixin:OnMouseDown()
	if self:IsEnabled() == 1 then
		self.down = true;

		if self.displacedRegions then
			local x, y = self.displaceX, self.displaceY;
			for index, region in ipairs(self.displacedRegions) do
				region:AdjustPointsOffset(x, y);
			end
		end

		self:OnButtonStateChanged();
		return true;
	end
	return false;
end

function ButtonStateBehaviorMixin:OnMouseUp()
	if self:IsEnabled() == 1 then
		self.down = nil;

		if self.displacedRegions then
			local x, y = self.displaceX, self.displaceY;
			for index, region in ipairs(self.displacedRegions) do
				region:AdjustPointsOffset(-x, -y);
			end
		end

		self:OnButtonStateChanged();

		return true;
	end
	return false;
end

function ButtonStateBehaviorMixin:OnEnable()
	self:OnButtonStateChanged();

	if self.desaturateIfDisabled then
		self:DesaturateHierarchy(0);
	end
end

function ButtonStateBehaviorMixin:OnDisable()
	self.over = nil;
	self.down = nil;

	if self.displacedRegions then
		local x, y = self.displaceX, self.displaceY;
		for index, region in ipairs(self.displacedRegions) do
			region:ClearPointsOffset();
		end
	end

	if self.desaturateIfDisabled then
		self:DesaturateHierarchy(1);
	end

	self:OnButtonStateChanged();
end