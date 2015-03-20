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

#include "protocolspectator.h"

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

ProtocolSpectator::ProtocolSpectator(Connection_ptr connection):
	ProtocolGame(connection),
	client(nullptr)
{

}

void ProtocolSpectator::deleteProtocolTask()
{
    Protocol::deleteProtocolTask();
}

void ProtocolSpectator::disconnectSpectator(const std::string& message)
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

void ProtocolSpectator::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		getConnection()->closeConnection();
		return;
	}

	operatingSystem = (OperatingSystem_t)msg.get<uint16_t>();
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
		opcodeMessage.Add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	msg.SkipBytes(1); // gamemaster flag
	std::string accountName = msg.GetString();
	std::string characterName = msg.GetString();
	std::string password = msg.GetString();

	uint32_t timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.GetByte();
	if (m_challengeTimestamp != timeStamp || m_challengeRandom != randNumber) {
		getConnection()->closeConnection();
		return;
	}

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		g_dispatcher.addTask(createTask(
				std::bind(&ProtocolSpectator::disconnectSpectator, this, "Only clients with protocol " CLIENT_VERSION_STR " allowed!")));
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::login, this, characterName, password)));
}

void ProtocolSpectator::sendEmptyTileOnPlayerPos(const Tile* tile, const Position& playerPos)
{
	NetworkMessage msg;

	msg.AddByte(0x69);
	msg.AddPosition(playerPos);

	msg.Add<uint16_t>(0x00);
	msg.AddItem(tile->ground);

	msg.AddByte(0x00);
	msg.AddByte(0xFF);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::addDummyCreature(NetworkMessage& msg, const uint32_t& creatureID, const Position& playerPos)
{
	// add dummy creature
	CreatureType_t creatureType = CREATURETYPE_NPC;
	if(creatureID <= 0x10000000) {
		creatureType = CREATURETYPE_PLAYER;
	} else if(creatureID <= 0x40000000) {
		creatureType = CREATURETYPE_MONSTER;
	}
	msg.AddByte(0x6A);
	msg.AddPosition(playerPos);
	msg.AddByte(1); //stackpos
	msg.Add<uint16_t>(0x61); // is not known
	msg.Add<uint32_t>(0); // remove no creature
	msg.Add<uint32_t>(creatureID); // creature id
	msg.AddByte(creatureType); // creature type
	msg.AddString("Dummy");
	msg.AddByte(0x00); // health percent
	msg.AddByte(DIRECTION_NORTH); // direction
	AddOutfit(msg, player->getCurrentOutfit()); // outfit
	msg.AddByte(0); // light level
	msg.AddByte(0); // light color
	msg.Add<uint16_t>(200); // speed
	msg.AddByte(SKULL_NONE); // skull type
	msg.AddByte(SHIELD_NONE); // party shield
	msg.AddByte(GUILDEMBLEM_NONE); // guild emblem
	msg.AddByte(creatureType); // creature type
	msg.AddByte(SPEECHBUBBLE_NONE); // speechbubble
	msg.AddByte(0xFF); // MARK_UNMARKED
	msg.Add<uint16_t>(0x00); // helpers
	msg.AddByte(0); // walkThrough
}

void ProtocolSpectator::syncKnownCreatureSets()
{
	const auto& casterKnownCreatures = client->getKnownCreatures();
	const auto playerPos = player->getPosition();
	const auto tile = player->getTile();

	if (!tile || !tile->ground) {
		disconnectSpectator("A sync error has occured.");
		return;
	}
	sendEmptyTileOnPlayerPos(tile, playerPos);

	bool known;
	uint32_t removedKnown;
	for (const auto creatureID : casterKnownCreatures) {
		NetworkMessage msg;
		const auto creature = g_game.getCreatureByID(creatureID);
		if (creature && !creature->isRemoved()) {
			msg.AddByte(0x6A);
			msg.AddPosition(playerPos);
			msg.AddByte(1); //stackpos
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);
			RemoveTileThing(msg, playerPos, 1);
		} else if (operatingSystem <= CLIENTOS_FLASH) { // otclient freeze with huge amount of creature add, but do not debug if there are unknown creatures, best solution for now :(
			addDummyCreature(msg, creatureID, playerPos);
			RemoveTileThing(msg, playerPos, 1);
		}
		writeToOutputBuffer(msg);
	}

	sendUpdateTile(tile, playerPos);
}

void ProtocolSpectator::syncChatChannels()
{
	const auto channels = g_chat->getChannelList(*player);
	for (const auto channel : channels) {
		const auto& channelUsers = channel->getUsers();
		if (channelUsers.find(player->getID()) != channelUsers.end()) {
			sendChannel(channel->getId(), channel->getName(), &channelUsers, channel->getInvitedUsersPtr());
		}
	}
	sendChannel(CHANNEL_CAST, LIVE_CAST_CHAT_NAME, nullptr, nullptr);
}

void ProtocolSpectator::syncOpenContainers()
{
	const auto openContainers = player->getOpenContainers();
	for (const auto& it : openContainers) {
		auto openContainer = it.second;
		auto container = openContainer.container;
		sendContainer(it.first, container, container->hasParent(), openContainer.index);
	}
}

void ProtocolSpectator::login(const std::string& liveCastName, const std::string& liveCastPassword)
{
	//dispatcher thread
	auto _player = g_game.getPlayerByName(liveCastName);
	if (!_player || _player->isRemoved()) {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
		return;
	}

	const auto liveCasterProtocol = getLiveCast(_player);

	if (!liveCasterProtocol) {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
		return;
	}

	const auto& password = liveCasterProtocol->getLiveCastPassword();
	if (liveCasterProtocol->isLiveCaster()) {
		if (password != liveCastPassword) {
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
		syncOpenContainers();

		liveCasterProtocol->addSpectator(this);
	} else {
		disconnectSpectator("Live cast no longer exists. Please relogin to refresh the list.");
	}
}

void ProtocolSpectator::logout()
{
	m_acceptPackets = false;
	if (client) {
		client->removeSpectator(this);
		client = nullptr;
		player = nullptr;
	}
	disconnect();
}

void ProtocolSpectator::parsePacket(NetworkMessage& msg)
{
	if (!m_acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() <= 0) {
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

	switch (recvbyte) {
		case 0x14: g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::logout, this))); break;
		case 0x1D: g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::sendPingBack, this))); break;
		case 0x1E: g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::sendPing, this))); break;
		case 0x96: parseSpectatorSay(msg); break;
		default:
			break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolSpectator::parseSpectatorSay(NetworkMessage& msg)
{
	SpeakClasses type = (SpeakClasses)msg.GetByte();
	uint16_t channelId = 0;

	if (type == TALKTYPE_CHANNEL_Y) {
		channelId = msg.get<uint16_t>();
	} else {
		return;
	}

	const std::string text = msg.GetString();

	if (text.length() > 255 || channelId != CHANNEL_CAST || !client) {
		return;
	}

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::broadcastSpectatorMessage, client, text)));
}

void ProtocolSpectator::releaseProtocol()
{
	if (client) {
		client->removeSpectator(this);
		client = nullptr;
		player = nullptr;
	}
	Protocol::releaseProtocol();
}

void ProtocolSpectator::writeToOutputBuffer(const NetworkMessage& msg)
{
	OutputMessage_ptr out = getOutputBuffer(msg.getLength());

	if (out) {
		out->append(msg);
	}
}

