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

#ifndef FS_PROTOCOLCAST_H
#define FS_PROTOCOLCAST_H

#include "protocolgame.h"


class ProtocolCast : public ProtocolGame
{
	public:
		static const char* protocol_name() {
			return "casting protocol";
		}

		ProtocolCast(Connection_ptr connection);
		virtual ~ProtocolCast();

		virtual int32_t getProtocolId() {
			return 0x0A;
		}

	private:

		ProtocolGame* client;

		void login(const std::string& liveCastName, const std::string& liveCastPassword);
		void logout();
		
		void disconnectSpectator(const std::string& message);
		void writeToOutputBuffer(const NetworkMessage& msg);
		
		void syncKnownCreatureSets();
		void syncChatChannels();
		
		virtual void releaseProtocol();
		virtual void deleteProtocolTask();

		virtual void parsePacket(NetworkMessage& msg);
		virtual void onRecvFirstMessage(NetworkMessage& msg);
		
};

#endif
