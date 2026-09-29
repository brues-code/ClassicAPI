-- The later clients' names for the video and sound settings windows, given to
-- the client's own windows.
--
-- Later clients (3.3.5, Classic Era) replaced vanilla's video options window
-- (OptionsFrame) and sound window (SoundOptionsFrame) with new ones,
-- VideoOptionsFrame and AudioOptionsFrame, and code written for them uses those
-- names: LibBetterBlizzOptions-1.0 makes both movable while loading, and
-- VideoOptionsFrame_Toggle / AudioOptionsFrame_Toggle show and hide them. Here
-- the names are the settings windows this client has, so that code acts on the
-- settings UI the player uses:
--   - VideoOptionsFrame is OptionsFrame;
--   - AudioOptionsFrame is SoundOptionsFrame, or OptionsFrame on clients whose
--     one options window also holds the sound settings (OctoWoW's).
-- The 3.x windows' own panels and buttons (VideoOptionsFrameOkay, the category
-- list) don't exist; the client's windows have theirs.

if not VideoOptionsFrame then
	VideoOptionsFrame = OptionsFrame
end
if not AudioOptionsFrame then
	AudioOptionsFrame = SoundOptionsFrame or OptionsFrame
end

local function Toggle(frame)
	if frame:IsShown() then
		frame:Hide()
	else
		frame:Show()
	end
end

if VideoOptionsFrame and not VideoOptionsFrame_Toggle then
	function VideoOptionsFrame_Toggle()
		Toggle(VideoOptionsFrame)
	end
end
if AudioOptionsFrame and not AudioOptionsFrame_Toggle then
	function AudioOptionsFrame_Toggle()
		Toggle(AudioOptionsFrame)
	end
end
