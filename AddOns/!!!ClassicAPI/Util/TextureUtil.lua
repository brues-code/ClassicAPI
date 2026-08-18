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
