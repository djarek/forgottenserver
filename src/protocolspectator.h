/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2014  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FS_PROTOCOLSPECTATOR_H
#define FS_PROTOCOLSPECTATOR_H

#include "protocolgame.h"


class ProtocolSpectator : public ProtocolGame
{
	public:
		static const char* protocol_name() {
			return "spectator protocol";
		}

		ProtocolSpectator(Connection_ptr connection);

	private:

		ProtocolGame* client;
		OperatingSystem_t operatingSystem;

		void login(const std::string& liveCastName, const std::string& liveCastPassword);
		void logout();
		
		void disconnectSpectator(const std::string& message);
		void writeToOutputBuffer(const NetworkMessage& msg);
		
		void syncKnownCreatureSets();
		void syncChatChannels();
		void syncOpenContainers();
		void sendEmptyTileOnPlayerPos(const Tile* tile, const Position& playerPos);
		
		void releaseProtocol() override;
		void deleteProtocolTask() override;

		void parsePacket(NetworkMessage& msg) override;
		void onRecvFirstMessage(NetworkMessage& msg) override;

		void parseSpectatorSay(NetworkMessage& msg);

};

#endif
