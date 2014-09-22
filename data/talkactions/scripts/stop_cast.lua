function onSay(cid, words, param)
	local player = Player(cid)
	if player:stopLiveCast(param) then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You have stopped casting your gameplay.")
		return false
	else
		player:sendCancelMessage("You're not casting your gameplay.")
		return false
	end
end