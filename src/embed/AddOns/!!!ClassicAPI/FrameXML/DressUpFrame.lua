-- 4.3.4's DressUpItemLink (FrameXML/ModelFrames.lua): the whole link goes to
-- the model, so DressUpModel:TryOn shows the link's enchant glow, and an item
-- the model cannot wear does not open the window. 1.12.1's version passes
-- only the item ID.
--
-- Differences from the 4.3.4 source:
--   - The link goes through DressUpItem, which 4.3.4 folded in. Here
--     Blizzard_AuctionUI replaces DressUpItem to show its own model, and
--     some clients add steps to it (Turtle dresses the tabard).
--   - An uncached item skips the IsDressableItem test, which reads only the
--     cache. 1.12.1 opens no tooltip before a chat link's Control-click, so
--     the test would drop most chat links; TryOn loads the item instead.

function DressUpItemLink(link)
	if ( not link or (C_Item.IsItemDataCachedByID(link) and not IsDressableItem(link)) ) then
		return;
	end
	DressUpItem(link);
end
