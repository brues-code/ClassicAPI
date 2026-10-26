-- CooldownFrame_SetTimer for both kinds of cooldown frame. A Cooldown frame
-- (CreateFrame("Cooldown") or <Cooldown>) takes 4.3.4's body (Cooldown.lua),
-- which hands the timer to the frame's own SetCooldown. Every other frame is
-- one of 1.12's indicator Models (CooldownFrameTemplate, as the action, bag,
-- spellbook and paper-doll buttons build theirs) and keeps 1.12's body, which
-- drives the model's sweep from CooldownFrame_OnUpdateModel.

function CooldownFrame_SetTimer(this, start, duration, enable)
    if ( this:IsObjectType("Cooldown") ) then
        if ( start and start > 0 and duration > 0 and enable > 0 ) then
            this:SetCooldown(start, duration);
            this:Show();
        else
            this:Hide();
        end
        return;
    end
    if ( start > 0 and duration > 0 and enable > 0) then
        this.start = start;
        this.duration = duration;
        this.stopping = 0;
        this:SetSequence(0);
        this:Show();
    else
        this:Hide();
    end
end

-- The later clients' helpers (Blizzard_FrameXMLUtil/Cooldown.lua). An
-- indicator Model has no Clear or SetDrawEdge, so it takes 1.12's timer.

function CooldownFrame_Set(self, start, duration, enable, forceShowDrawEdge, modRate)
    if ( not self:IsObjectType("Cooldown") ) then
        CooldownFrame_SetTimer(self, start, duration, (enable and enable ~= 0) and 1 or 0);
        return;
    end
    if enable and enable ~= 0 and start > 0 and duration > 0 then
        self:SetDrawEdge(forceShowDrawEdge);
        self:SetCooldown(start, duration, modRate);
    else
        CooldownFrame_Clear(self);
    end
end

function CooldownFrame_Clear(self)
    if ( not self:IsObjectType("Cooldown") ) then
        self:Hide();
        return;
    end
    self:Clear();
end

function CooldownFrame_SetDisplayAsPercentage(self, percentage)
    local seconds = 100;	-- any number, really
    self:Pause();
    self:SetCooldown(GetTime() - (seconds * Saturate(percentage)), seconds);
end
