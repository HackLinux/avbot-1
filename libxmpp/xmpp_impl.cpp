/*
 * Copyright (C) 2012  微蔡 <microcai@fedoraproject.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <gloox/message.h>
#include <gloox/mucroom.h>

#include <boost/lexical_cast.hpp>

#include "boost/timedcall.hpp"
#include "avproxy/avproxy.hpp"

#include "xmpp_impl.h"

using namespace XMPP;

xmpp_impl::xmpp_impl(boost::asio::io_service& asio, std::string xmppuser, std::string xmpppasswd, std::string xmppserver, std::string xmppnick)
:gloox::ConnectionBase(&m_client), io_service(asio), m_jid(xmppuser+"/"+xmppnick), m_client(m_jid, xmpppasswd), m_xmppnick(xmppnick)
{
	m_client.registerConnectionListener(this);
	m_client.registerMessageHandler(this);
	
	if(!xmppserver.empty()){
		std::vector<std::string> splited;
		// 设定服务器.
		boost::split(splited, xmppserver, boost::is_any_of(":"));
		m_client.setServer(splited[0]);
		if(splited.size() == 2)
			m_client.setPort(boost::lexical_cast<int>(splited[1]));
	}
	io_service.post(boost::bind(&xmpp_impl::start, this));
}

void xmpp_impl::start()
{
	if (state() == gloox::StateDisconnected){
		m_asio_socket.reset(new boost::asio::ip::tcp::socket(io_service));
		std::string xmppclientport ;
		if (m_client.port() == -1)
			xmppclientport = "xmpp-client";
		else
			xmppclientport = boost::lexical_cast<std::string>(m_client.port());

		avproxy::proxy::tcp::query query(m_client.server(), xmppclientport);
		avproxy::async_proxy_connect(avproxy::autoproxychain(*m_asio_socket, query), 
			boost::bind(&xmpp_impl::cb_handle_connecting, this, _1));
		m_state = gloox::StateConnecting;
	}
}

void xmpp_impl::cb_handle_connecting(const boost::system::error_code & ec)
{
	if (ec)
	{
		std::cerr <<  "xmpp服务器链接错误:" <<  ec.message() <<  std::endl;
		// 重试.
		m_asio_socket.reset(new boost::asio::ip::tcp::socket(io_service));

		std::string xmppclientport ;
		if (m_client.port() == -1)
			xmppclientport = "xmpp-client";
		else
			xmppclientport = boost::lexical_cast<std::string>(m_client.port());

		avproxy::proxy::tcp::query query(m_client.server(), xmppclientport);
	
		avproxy::async_proxy_connect(avproxy::autoproxychain(*m_asio_socket, query), 
			boost::bind(&xmpp_impl::cb_handle_connecting, this, _1));
		return ;
	}

	m_client.setConnectionImpl(this);

	if(m_client.connect(false))
		io_service.post(boost::bind(&xmpp_impl::cb_handle_connected, this));
	else{
		std::cerr << "unable to connect to xmpp server" << std::endl;
	}
}

void xmpp_impl::cb_handle_connected()
{
	m_state = gloox::StateConnected;
	m_client.handleConnect(this);

	m_asio_socket->async_read_some(m_readbuf.prepare(8192),
		boost::bind(&xmpp_impl::cb_handle_asio_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
	);
}

void xmpp_impl::join(std::string roomjid)
{
	gloox::JID roomnick(roomjid+"/" + m_xmppnick);//"avplayer@im.linuxapp.org";
	boost::shared_ptr<gloox::MUCRoom> room( new  gloox::MUCRoom(&m_client, roomnick, this));
	m_rooms.push_back(room);
}

void xmpp_impl::cb_handle_asio_read(const boost::system::error_code& error, std::size_t bytes_transferred)
{
	if (error){
		m_client.handleDisconnect(this, gloox::ConnStreamClosed);
	}else{
		m_readbuf.commit(bytes_transferred);
		std::string data(boost::asio::buffer_cast<const char*>(m_readbuf.data()), m_readbuf.size());
		m_client.handleReceivedData(this, data);
		m_readbuf.consume(m_readbuf.size());
		m_asio_socket->async_read_some(m_readbuf.prepare(8102), 
		boost::bind(&xmpp_impl::cb_handle_asio_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
		);
	}
}

void xmpp_impl::cb_handle_asio_write(const boost::system::error_code& error, std::size_t bytes_transferred)
{
	if (error)
		m_client.handleDisconnect(this,  gloox::ConnStreamError);
}

bool xmpp_impl::send(const std::string& data)
{
	if (m_asio_socket->is_open()){
		// write to socket
		m_asio_socket->async_write_some(boost::asio::buffer(data),
			boost::bind(&xmpp_impl::cb_handle_asio_write, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
		);
		return true;
	}
	m_client.handleDisconnect(this,  gloox::ConnStreamClosed);
	return false;	
}

gloox::ConnectionError xmpp_impl::connect()
{
	return m_asio_socket->is_open() ?  gloox::ConnNoError: gloox::ConnNotConnected;
}

gloox::ConnectionError xmpp_impl::receive()
{
	BOOST_ASSERT(1);
}

gloox::ConnectionBase* xmpp_impl::newInstance() const
{
	const gloox::ConnectionBase* ret = this;
	return (gloox::ConnectionBase*)(ret);
}

gloox::ConnectionError xmpp_impl::recv(int timeout)
{

}

void xmpp_impl::getStatistics(long int& totalIn, long int& totalOut)
{

}

void xmpp_impl::disconnect()
{
	m_state = gloox::StateDisconnected;
}

void xmpp_impl::on_room_message(boost::function<void (std::string xmpproom, std::string who, std::string message)> cb)
{
	m_sig_room_message.connect(cb);
}

void xmpp_impl::send_room_message(std::string xmpproom, std::string message)
{
	//查找啊.
	BOOST_FOREACH(boost::shared_ptr<gloox::MUCRoom> room, m_rooms)
	{
		if ( room->name() == xmpproom ){
			room->send(message);
		}
	}	
}

void xmpp_impl::handleMessage(const gloox::Message& stanza, gloox::MessageSession* session)
{
	std::cout <<  __func__ <<  std::endl;
	gloox::Message msg( gloox::Message::Chat , stanza.from(), "hello world" );
    m_client.send( msg );
}

void xmpp_impl::handleMUCMessage(gloox::MUCRoom* room, const gloox::Message& msg, bool priv)
{
	if (!msg.from().resource().empty() && msg.from().resource() != room->nick())
		m_sig_room_message(room->name(), msg.from().resource(), msg.body());
}

bool xmpp_impl::onTLSConnect(const gloox::CertInfo& info)
{
	return true;
}

void xmpp_impl::onConnect()
{
	BOOST_FOREACH(boost::shared_ptr<gloox::MUCRoom> room,  m_rooms)
	{
		room->join();
		room->getRoomInfo();
		room->getRoomItems();
	}
}

void xmpp_impl::onDisconnect(gloox::ConnectionError e)
{
	boost::delayedcallsec(io_service, 10, boost::bind(&xmpp_impl::start, this));
}

static std::string randomname(std::string m_xmppnick)
{
	return boost::str(boost::format("%s%X") % m_xmppnick % random());
}

void xmpp_impl::handleMUCError(gloox::MUCRoom* room, gloox::StanzaError error)
{
	std::cout <<  __func__ <<  std::endl;
	if (error == gloox::StanzaErrorConflict)
	{
		// 出现名字冲突，使用一个随机名字.
		room->setNick(randomname(m_xmppnick));
		room->join();
	}
}

void xmpp_impl::handleMUCInfo(gloox::MUCRoom* room, int features, const std::string& name, const gloox::DataForm* infoForm)
{
	std::cout <<  __func__ <<  std::endl;
}

void xmpp_impl::handleMUCInviteDecline(gloox::MUCRoom* room, const gloox::JID& invitee, const std::string& reason)
{
	std::cout <<  __func__ <<  std::endl;

}

void xmpp_impl::handleMUCItems(gloox::MUCRoom* room, const gloox::Disco::ItemList& items)
{
	std::cout <<  __func__ <<  std::endl;

}


void xmpp_impl::handleMUCParticipantPresence(gloox::MUCRoom* room, const gloox::MUCRoomParticipant participant, const gloox::Presence& presence)
{
	std::cout <<  __func__ <<  std::endl;

}

bool xmpp_impl::handleMUCRoomCreation(gloox::MUCRoom* room)
{
	std::cout <<  __func__ <<  std::endl;
	return false;
}

void xmpp_impl::handleMUCSubject(gloox::MUCRoom* room, const std::string& nick, const std::string& subject)
{
	std::cout <<  __func__ <<  std::endl;

}

