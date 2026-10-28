-- Backport of retail's Blizzard_SharedXMLBase/TemplateInfoCache.lua.
--
-- Retail reads each template's info through a forbidden delegate frame
-- (SetForbidden, then SetAttribute into an OnAttributeChanged handler) so the
-- lookup runs untainted. 1.12 has no taint, so GetTemplateInfo asks
-- C_XMLUtil.GetTemplateInfo directly; the cache and its callback are retail's.

TemplateInfoCacheMixin = {};

function TemplateInfoCacheMixin:Init()
	self.templateInfos = {};
end

function TemplateInfoCacheMixin:SetInfoAddedCallback(callback)
	self.infoAddedCallback = callback;
end

function TemplateInfoCacheMixin:FlushTemplateInfos()
	self.templateInfos = {};
end

function TemplateInfoCacheMixin:GetTemplateInfo(frameTemplate)
	local info = self.templateInfos[frameTemplate];
	if info then
		return info;
	end

	local newInfo = C_XMLUtil.GetTemplateInfo(frameTemplate);
	self.templateInfos[frameTemplate] = newInfo;

	if self.infoAddedCallback then
		self.infoAddedCallback(newInfo);
	end

	return newInfo;
end

function TemplateInfoCacheMixin:GetTemplateInfos()
	return self.templateInfos;
end

function CreateTemplateInfoCache()
	local cache = CreateFromMixins(TemplateInfoCacheMixin);
	cache:Init();
	return cache;
end
