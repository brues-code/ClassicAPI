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
