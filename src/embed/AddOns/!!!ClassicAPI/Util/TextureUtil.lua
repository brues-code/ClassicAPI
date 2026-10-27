TextureUtil = {};

function SetClampedTextureRotation(texture, rotationDegrees)
    if (rotationDegrees ~= 0 and rotationDegrees ~= 90 and rotationDegrees ~= 180 and rotationDegrees ~= 270) then
        error("SetRotation: rotationDegrees must be 0, 90, 180, or 270");
        return;
    end

    if not (texture.rotationDegrees) then
        texture.origTexCoords = {texture:GetTexCoord()};
        texture.origWidth = texture:GetWidth();
        texture.origHeight = texture:GetHeight();
    end

    if (texture.rotationDegrees == rotationDegrees) then
        return;
    end

    texture.rotationDegrees = rotationDegrees;

    if (rotationDegrees == 0 or rotationDegrees == 180) then
        texture:SetWidth(texture.origWidth);
        texture:SetHeight(texture.origHeight);
    else
        texture:SetWidth(texture.origHeight);
        texture:SetHeight(texture.origWidth);
    end

    if (rotationDegrees == 0) then
        texture:SetTexCoord( texture.origTexCoords[1], texture.origTexCoords[2],
                             texture.origTexCoords[3], texture.origTexCoords[4],
                             texture.origTexCoords[5], texture.origTexCoords[6],
                             texture.origTexCoords[7], texture.origTexCoords[8] );
    elseif (rotationDegrees == 90) then
        texture:SetTexCoord( texture.origTexCoords[3], texture.origTexCoords[4],
                             texture.origTexCoords[7], texture.origTexCoords[8],
                             texture.origTexCoords[1], texture.origTexCoords[2],
                             texture.origTexCoords[5], texture.origTexCoords[6] );
    elseif (rotationDegrees == 180) then
        texture:SetTexCoord( texture.origTexCoords[7], texture.origTexCoords[8],
                             texture.origTexCoords[5], texture.origTexCoords[6],
                             texture.origTexCoords[3], texture.origTexCoords[4],
                             texture.origTexCoords[1], texture.origTexCoords[2] );
    elseif (rotationDegrees == 270) then
        texture:SetTexCoord( texture.origTexCoords[5], texture.origTexCoords[6],
                             texture.origTexCoords[1], texture.origTexCoords[2],
                             texture.origTexCoords[7], texture.origTexCoords[8],
                             texture.origTexCoords[3], texture.origTexCoords[4] );
    end
end

function ClearClampedTextureRotation(texture)
    if (texture.rotationDegrees) then
        SetClampedTextureRotation(texture, 0);
        texture.origTexCoords = nil;
        texture.origWidth = nil;
        texture.origHeight = nil;
        texture.rotationDegrees = nil;
    end
end

function GetTexCoordsByGrid(xOffset, yOffset, textureWidth, textureHeight, gridWidth, gridHeight)
    local widthPerGrid = gridWidth / textureWidth;
    local heightPerGrid = gridHeight / textureHeight;
    return (xOffset - 1) * widthPerGrid, (xOffset) * widthPerGrid, (yOffset - 1) * heightPerGrid, (yOffset) * heightPerGrid;
end

-- left/right/top/bottom are normalized [0..1] coords within the file (the
-- markup's texel fields are produced by multiplying back up by the file size).
function CreateTextureMarkup(file, fileWidth, fileHeight, width, height, left, right, top, bottom, xOffset, yOffset)
    return string.format("|T%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d|t",
          file
        , height
        , width
        , xOffset or 0
        , yOffset or 0
        , fileWidth
        , fileHeight
        , left * fileWidth
        , right * fileWidth
        , top * fileHeight
        , bottom * fileHeight
    );
end

function CreateSimpleTextureMarkup(file, width, height, xOffset, yOffset)
    return string.format("|T%s:%d:%d:%d:%d|t",
          file
        , height or width
        , width
        , xOffset or 0
        , yOffset or 0
    );
end

function CreateAtlasMarkup(atlasName, width, height, offsetX, offsetY)
    return string.format("|A:%s:%d:%d:%d:%d|a",
          atlasName
        , height or 0
        , width or 0
        , offsetX or 0
        , offsetY or 0
    );
end

-- Raid target markers, published as atlas names.
--
-- All eight markers live in one 4x4 sheet, so naming a single marker means
-- naming a cell inside that sheet — exactly what an atlas is for. Blizzard
-- publishes no atlas names for these, so the names here follow the standalone
-- marker files that other clients ship. An addon written against those files
-- finds the art under the name it already knows.
--
-- Marker N sits at column (N-1) mod 4 and row floor((N-1) / 4), counting from
-- the top left. That is the same cell math the chat marker tokens use.
local RAID_TARGET_SHEET = "Interface\\TargetingFrame\\UI-RaidTargetingIcons";
local RAID_TARGET_COLUMNS = 4;
local RAID_TARGET_CELL = 0.25; -- a 64 pixel cell in a 256 pixel sheet

for marker = 1, 8 do
    local index = marker - 1;
    local left = math.mod(index, RAID_TARGET_COLUMNS) * RAID_TARGET_CELL;
    local top = math.floor(index / RAID_TARGET_COLUMNS) * RAID_TARGET_CELL;
    C_Texture.RegisterAtlas("UI-RaidTargetingIcon_"..marker, RAID_TARGET_SHEET,
                            64, 64,
                            left, left + RAID_TARGET_CELL,
                            top, top + RAID_TARGET_CELL);
end
