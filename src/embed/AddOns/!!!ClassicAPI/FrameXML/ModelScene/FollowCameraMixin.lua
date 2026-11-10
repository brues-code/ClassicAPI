-- ClassicAPI extension: an orbit camera that follows an actor and stops in
-- front of the WMO between the actor and the eye.

--[[
	FollowCameraMixin - An OrbitCamera whose target is an actor's position plus a height, each frame.

	The eye is placed the way the world's follow camera places it (the boom sweep of its solver 0x0050E570):
	a trace of the camera's faces from the target to the eye (0x0050E816) shortens the distance, which then
	backs off 1/9 yard (0x0050E871 to 0x0050E8A4). With no actor to follow, it is a plain OrbitCamera.

	Create it beside the scene's own camera and give it that camera's settings:
		local _, cameraIDs = C_ModelInfo.GetModelSceneInfoByID(modelSceneID);
		local camera = scene:AddCamera(CameraRegistry:CreateCameraByType("FollowCamera"));
		camera:ApplyFromModelSceneCameraInfo(C_ModelInfo.GetModelSceneCameraInfoByID(cameraIDs[1]), CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD);
		camera:SetFollowActor(actor, 1.6);
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

-- "public" functions
function FollowCameraMixin:GetCameraType() -- override
	return CAMERA_NAME;
end

-- The target becomes the actor's position, `height` yards up. Nil stops following.
function FollowCameraMixin:SetFollowActor(actor, height)
	self.followActor = actor;
	self.followHeight = height or 0;
end

function FollowCameraMixin:GetFollowActor()
	return self.followActor, self.followHeight;
end

-- "private" functions
function FollowCameraMixin:OnUpdate(elapsed) -- override
	local actor = self.followActor;
	if actor then
		local x, y, z = actor:GetPosition();
		self:SetTarget(x, y, z + self.followHeight);
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
