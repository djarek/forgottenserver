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

#include "otpch.h"

#include "protocolcast.h"

#include "networkmessage.h"
#include "outputmessage.h"

#include "tile.h"
#include "player.h"
#include "chat.h"

#include "configmanager.h"

#include "game.h"

#include "connection.h"
#include "scheduler.h"

extern Game g_game;
extern ConfigManager g_config;
extern Chat* g_chat;

ProtocolCast::ProtocolCast(Connection_ptr connection):
	ProtocolGame(connection),
	client(nullptr)
{

}

ProtocolCast::~ProtocolCast()
{

}

void ProtocolCast::deleteProtocolTask()
{
    Protocol::deleteProtocolTask();
}

void ProtocolCast::disconnectSpectator(const std::string& message)
{
	if (client) {
		client->removeSpectator(this);
		player = nullptr;
		client = nullptr;
	}

	OutputMessage_ptr output = OutputMessagePool::getInstance()->getOutputMessage(this, false);
	if (output) {
		output->AddByte(0x14);
		output->AddString(message);
		OutputMessagePool::getInstance()->send(output);
	}
	disconnect();
}

void ProtocolCast::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		getConnection()->closeConnection();
		return;
	}

	OperatingSystem_t operatingSystem = (OperatingSystem_t)msg.get<uint16_t>();
	version = msg.get<uint16_t>();

	msg.SkipBytes(5); // U32 clientVersion, U8 clientType

	if (!RSA_decrypt(msg)) {
		getConnection()->closeConnection();
		return;
	}

	uint32_t key[4];
	key[0] = msg.get<uint32_t>();
	key[1] = msg.get<uint32_t>();
	key[2] = msg.get<uint32_t>();
	key[3] = msg.get<uint32_t>();
	enableXTEAEncryption();
	setXTEAKey(key);

	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		NetworkMessage opcodeMessage;
		opcodeMessage.AddByte(0x32);
		opcodeMessage.AddByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	msg.SkipBytes(1); // gamemaster flag
	msg.GetString(); //skip account name
	std::string characterName = msg.GetString(); //Skip characterName
	std::string password = msg.GetString();

	uint32_t timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.GetByte();
	if (m_challengeTimestamp != timeStamp || m_challengeRandom != randNumber) {
		getConnection()->closeConnection();
		return;
	}

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		g_dispatcher.addTask(createTask(
				std::bind(&ProtocolCast::disconnectSpectator, this, "Only clients with protocol " CLIENT_VERSION_STR " allowed!")));
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolCast::login, this, characterName, password)));
}

void ProtocolCast::syncKnownCreatureSets()
{
	const auto& casterKnownCreatures = client->getKnownCreatures();
	const auto playerStackPos = player->getTile()->getStackposOfCreature(player, player);

	for (const auto creatureID : casterKnownCreatures) {
		const auto creature = g_game.getCreatureByID(creatureID);
		if (creature && !creature->isRemoved()) {
			if (knownCreatureSet.find(creatureID) != knownCreatureSet.end()) {
				continue;
			}

			NetworkMessage msg;
			sendAddCreature(creature, player->getPosition(), playerStackPos, false);
			RemoveTileThing(msg, player->getPosition(), playerStackPos);
			writeToOutputBuffer(msg);
		}
	}
}

void ProtocolCast::syncChatChannels()
{
	auto channels = g_chat->getChannelList(*player);
	for (const auto channel : channels) {
		const auto& channelUsers = channel->getUsers();
		if (channelUsers.find(player->getID()) != channelUsers.end()) {
			sendChannel(channel->getId(), channel->getName(), &channelUsers, channel->getInvitedUsersPtr());
		}
	}
	sendChannel(CHANNEL_CAST, LIVE_CAST_CHAT_NAME, nullptr, nullptr);
}

void ProtocolCast::login(const std::string& liveCastName, const std::string& liveCastPassword)
{
	//dispatcher thread
	auto _player = g_game.getPlayerByName(liveCastName);
	if (!_player || _player->isRemoved()) {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
		return;
	}

	auto liveCasterProtocol = getLiveCast(_player);

	if (!liveCasterProtocol) {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
		return;
	}

	const auto& password = liveCasterProtocol->getLiveCastPassword();
	if (liveCasterProtocol->isLiveCaster()) {
		if (!password.empty() && password != liveCastPassword) {
			disconnectSpectator("Wrong live cast password.");
			return;
		}

		player = _player;
		eventConnect = 0;
		client = liveCasterProtocol;
		m_acceptPackets = true;

		sendAddCreature(player, player->getPosition(), 0, false);
		syncKnownCreatureSets();
		syncChatChannels();

		liveCasterProtocol->addSpectator(this);
	} else {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
	}
}

void ProtocolCast::logout()
{
	m_acceptPackets = false;
	if (client) {
		client->removeSpectator(this);
		client = nullptr;
		player = nullptr;
	}
	disconnect();
}

void ProtocolCast::parsePacket(NetworkMessage& msg)
{
	if (!m_acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getMessageLength() <= 0) {
		return;
	}

	uint8_t recvbyte = msg.GetByte();

	if (!player) {
		if (recvbyte == 0x0F) {
			disconnect();
		}

		return;
	}

	//a dead player can not perform actions
	if (player->isRemoved() || player->getHealth() <= 0) {
		disconnect();
		return;
	}

	switch(recvbyte) {
		case 0x14: g_dispatcher.addTask(createTask(std::bind(&ProtocolCast::logout, this))); break;
		case 0x1D: g_dispatcher.addTask(createTask(std::bind(&ProtocolCast::sendPingBack, this))); break;
		case 0x1E: g_dispatcher.addTask(createTask(std::bind(&ProtocolCast::sendPing, this))); break;
		case 0x96: parseSpectatorSay(msg); break;
		default:
			break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolCast::parseSpectatorSay(NetworkMessage& msg)
{
	SpeakClasses type = (SpeakClasses)msg.GetByte();
	uint16_t channelId = 0;

	if (type != TALKTYPE_CHANNEL_Y) {
		return;
	}

	channelId = msg.get<uint16_t>();
	const std::string text = msg.GetString();

	if (text.length() > 255 || channelId != CHANNEL_CAST || !client) {
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::broadcastSpectatorMessage, client, text)));
}

void ProtocolCast::releaseProtocol()
{
	if (client) {
		client->removeSpectator(this);
		client = nullptr;
		player = nullptr;
	}
	Protocol::releaseProtocol();
}

void ProtocolCast::writeToOutputBuffer(const NetworkMessage& msg)
{
	OutputMessage_ptr out = getOutputBuffer(msg.getMessageLength());

	if (out) {
		out->append(msg);
	}
}
