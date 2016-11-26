/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2016  Mark Samman <mark.samman@gmail.com>
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

#include "../otpch.h"

#include "peer.h"
#include "server.h"
#include "router.h"
#include "responder.h"
#include "../tasks.h"

namespace http
{

using Seconds = std::chrono::seconds;
using PeerWeakPtr = std::weak_ptr<Peer>;

Peer::Peer(ApiServer& server, Router& router) :
	server(server),
	router(router),
	socket(server.getIoService()),
	timer(server.getIoService()),
	strand(server.getIoService())
{
	//
}

Peer::~Peer()
{
	cancelTimer();
}


void Peer::onAccept()
{
	read();
}

void Peer::read()
{
	auto sharedThis = shared_from_this();
	state = KeepAlive;
	startTimer(TimerType::Read);
	beast::http::async_read(socket, buffer, request, strand.wrap([sharedThis, this](ErrorCode err) {
		if (err) {
			std::cerr << "HTTP API peer read() error: " << err.message() << std::endl;
			internalClose();
			return;
		}

		cancelTimer(); // cancel read timer
		state = beast::http::is_keep_alive(request) ? KeepAlive : Close;

		startTimer(TimerType::ResponseGeneration);
		auto functor = std::bind(&Router::handleRequest, &router, Responder{sharedThis, std::move(request), requestCounter});
		g_dispatcher.addTask(createTask(std::move(functor)));
	}));
}

void Peer::write()
{
	cancelTimer(); // cancel response generation timer
	if (state != KeepAlive) {
		response.fields.replace("Connection", "close");
	}
	beast::http::prepare(response);
	auto sharedThis = shared_from_this();
	startTimer(TimerType::Write);
	++requestCounter;
	beast::http::async_write(socket, response, strand.wrap([sharedThis, this](ErrorCode err) {
		if (err) {
			std::cerr << "HTTP API peer write() error: " << err.message() << std::endl;
			internalClose();
			return;
		}

		if (state == KeepAlive) {
			cancelTimer(); //cancel write timer
			read();
		} else {
			internalClose();
		}
	}));
}

void Peer::internalClose()
{
	ErrorCode err{};
	socket.shutdown(asio::socket_base::shutdown_both, err);
	socket.close(err);
	cancelTimer();
	server.onPeerClose(*this);
}

void Peer::close()
{
	auto sharedThis = shared_from_this();
	strand.dispatch([sharedThis]() {
		sharedThis->internalClose();
	});
}

void Peer::startTimer(TimerType type)
{
	timer.expires_from_now(Seconds{5});
	PeerWeakPtr weakThis = shared_from_this();
	timer.async_wait(strand.wrap([weakThis, type](ErrorCode err) {
		if (err == asio::error::operation_aborted) {
			return;
		}

		auto sharedThis = weakThis.lock();
		if (sharedThis == nullptr) {
			return;
		}

		switch(type) {
			case TimerType::Write:
				//[[fallthrough]]
			case TimerType::Read:
				sharedThis->internalClose();
				break;
			case TimerType::ResponseGeneration:
				sharedThis->sendInternalServerError();
				break;
		}
	}));
}

void Peer::cancelTimer()
{
	ErrorCode err;
	timer.cancel(err);
	if (err) {
		std::cerr << "HTTP API Peer timer cancel error: " << err.message() << std::endl;
	}
}

void Peer::sendInternalServerError()
{
	response = {};
	response.status = 500;
	response.reason = "Internal Server Error";
	response.version = 11;
	write();
}

void Peer::send(Response response, RequestID requestID, ConnectionState state)
{
	auto sharedThis = shared_from_this();
	strand.dispatch([sharedThis, this, requestID, response, state]() {
		if (requestCounter != requestID) {
			std::cerr << "HTTP API Peer send error: invalid request ID" << std::endl;
			return;
		}
		this->response = std::move(response);
		if (state == Close) {
			this->response.fields.replace("Connection", "close");
			this->state = Close;
		}
		write();
	});
}

} //namespace http