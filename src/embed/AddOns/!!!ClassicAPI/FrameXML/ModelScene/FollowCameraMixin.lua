-- ClassicAPI extension: an orbit camera that follows an actor and stops in
-- front of the WMO between the actor and the eye.

--[[
	FollowCameraMixin - An OrbitCamera whose target is an actor's pivot: its position, raised by the pivot height.

	The pivot height is the one the world's follow camera eases to for a unit (actor:GetCameraPivotHeight), and
	it eases the same way: the camera's first height is taken at once (BeginHeightTransition 0x00512790), and
	each new one is armed (ArmHeight 0x005126B0) for |change| / cameraHeightSmoothSpeed seconds and eased with
	the camera's cosine curve (0x0050F160). Until the actor's model has loaded, the camera waits, as the world's
	does (0x0050E900).

	The eye is placed the way the world's follow camera places it (the boom sweep of its solver 0x0050E570):
	a trace of the camera's faces from the pivot to the eye (0x0050E816) shortens the distance, which then
	backs off 1/9 yard (0x0050E871 to 0x0050E8A4). With no actor to follow, it is a plain OrbitCamera.

	Create it beside the scene's own camera and give it that camera's settings:
		local _, cameraIDs = C_ModelInfo.GetModelSceneInfoByID(modelSceneID);
		local camera = scene:AddCamera(CameraRegistry:CreateCameraByType("FollowCamera"));
		camera:ApplyFromModelSceneCameraInfo(C_ModelInfo.GetModelSceneCameraInfoByID(cameraIDs[1]), CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD);
		camera:SetFollowActor(actor);
		scene:SetActiveCamera(camera);
]]

FollowCameraMixin = CreateFromMixins(OrbitCameraMixin);

local CAMERA_NAME = "FollowCamera";
CameraRegistry:AddCameraFactoryFromMixin(CAMERA_NAME, FollowCameraMixin);

-- The solver skips the sweep when the distance is not past the near clip by
-- more than this (0x008026BC, 2^-20).
local SWEEP_MIN_DISTANCE = 9.5367431640625e-07;
-- A distance shortened by more than this (0x008029D4, 2^-22) backs off by
-- BOOM_BACKOFF (0x00808A00, 1/9 yard), to no less than 0.
local SHORTENED_EPSILON = 2.384185791015625e-07;
local BOOM_BACKOFF = 1 / 9;
-- A height within this of the armed or the live one arms nothing (0x00801360).
local ARM_EPSILON = 0.001;
-- The ease runs while the live height is this far or more from its target
-- (0x008029D4, 2^-22).
local EASE_EPSILON = 2.384185791015625e-07;

-- "public" functions
function FollowCameraMixin:GetCameraType() -- override
	return CAMERA_NAME;
end

-- The target becomes the actor's pivot. Nil stops following.
function FollowCameraMixin:SetFollowActor(actor)
	self.followActor = actor;
end

function FollowCameraMixin:GetFollowActor()
	return self.followActor;
end

-- "private" functions

-- ArmHeight (0x005126B0) with no delay and a factor of 1, and its inner
-- BeginHeightTransition (0x00512790).
function FollowCameraMixin:ArmPivotHeight(height, now)
	local live = self.pivotHeight or 0;
	if self.pivotEasing and math.abs(self.pivotTarget - height) < ARM_EPSILON then
		return;
	end
	if math.abs(live - height) < ARM_EPSILON then
		return;
	end
	self.pivotTarget = height;
	if not self.pivotSnapped then
		self.pivotSnapped = true;
		self.pivotHeight = height;
		self.pivotEasing = false;
		return;
	end
	self.pivotFrom = live;
	self.pivotStart = now;
	self.pivotDuration = math.abs(height - live) / tonumber(GetCVar("cameraHeightSmoothSpeed"));
	self.pivotEasing = true;
end

-- The height channel of the camera's ease (0x0050F160).
function FollowCameraMixin:EasePivotHeight(now)
	if math.abs(self.pivotTarget - self.pivotHeight) < EASE_EPSILON then
		self.pivotEasing = false;
		return;
	end
	if not self.pivotEasing or self.pivotStart > now then
		return;
	end
	local t = (now - self.pivotStart) / self.pivotDuration;
	if t >= 1 then
		self.pivotHeight = self.pivotTarget;
	else
		self.pivotHeight = self.pivotFrom + (self.pivotTarget - self.pivotFrom) * (1 - math.cos(math.pi * t)) * 0.5;
	end
end

function FollowCameraMixin:OnUpdate(elapsed) -- override
	local actor = self.followActor;
	if actor then
		local height = actor:GetCameraPivotHeight(self:GetZoomDistance());
		if not height then
			return;
		end
		local now = GetTime();
		self:ArmPivotHeight(height, now);
		self:EasePivotHeight(now);
		local x, y, z = actor:GetPosition();
		self:SetTarget(x, y, z + self.pivotHeight);
	end
	OrbitCameraMixin.OnUpdate(self, elapsed);
end

function FollowCameraMixin:CalculatePositionByDistanceFromTarget(targetX, targetY, targetZ, zoomDistance, axisAngleX, axisAngleY, axisAngleZ) -- override
	if not self.followActor then
		return OrbitCameraMixin.CalculatePositionByDistanceFromTarget(self, targetX, targetY, targetZ, zoomDistance, axisAngleX, axisAngleY, axisAngleZ);
	end

	local scene = self:GetOwningScene();
	local distance = zoomDistance;
	if distance - scene:GetCameraNearClip() > SWEEP_MIN_DISTANCE then
		local eyeX, eyeY, eyeZ = OrbitCameraMixin.CalculatePositionByDistanceFromTarget(self, targetX, targetY, targetZ, distance, axisAngleX, axisAngleY, axisAngleZ);
		local _, _, _, _, _, _, fraction = scene:TraceSegment(targetX, targetY, targetZ, eyeX, eyeY, eyeZ, true);
		if fraction then
			distance = distance * fraction;
		end
		if math.abs(zoomDistance - distance) > SHORTENED_EPSILON then
			distance = math.max(distance - BOOM_BACKOFF, 0);
		end
	end
	return OrbitCameraMixin.CalculatePositionByDistanceFromTarget(self, targetX, targetY, targetZ, distance, axisAngleX, axisAngleY, axisAngleZ);
end
