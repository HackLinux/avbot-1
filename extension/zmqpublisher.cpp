#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/random.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/async_coro_queue.hpp>
#include <boost/thread.hpp>
#include <boost/circular_buffer.hpp>

#include "zmq.h"

#include "zmqpublisher.hpp"

class ZmqPublisher : boost::noncopyable
{
public:
	struct Message {
		std::string channel_name;
		std::string body;
	};

	ZmqPublisher()
		: io_()
		, work_(io_)
		, queue_(io_, 10)
	{
		ctx_.reset(zmq_ctx_new(), zmq_ctx_term);
		socket_.reset(zmq_socket(ctx_.get(), ZMQ_PUB), zmq_close);
		zmq_bind(socket_.get(), "tcp://*:8123");
		queue_.async_pop(boost::bind(&ZmqPublisher::on_queue_pop, this, _1, _2));
		boost::thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_))).detach();
	}

	virtual ~ZmqPublisher()
	{
		queue_.cancele();
		io_.stop();
	}

	void send(const std::string& channel, const std::string& str)
	{
		Message msg;
		msg.channel_name = channel;
		msg.body = str;
		queue_.push(msg);
	}

	void on_queue_pop(boost::system::error_code ec, const Message& value)
	{
		zmq_send(socket_.get(), value.channel_name.c_str(), value.channel_name.size(), ZMQ_SNDMORE);
		zmq_send(socket_.get(), value.body.c_str(), value.body.size(), 0);
		queue_.async_pop(boost::bind(&ZmqPublisher::on_queue_pop, this, _1, _2));
	}

private:
	boost::asio::io_service io_;
	boost::asio::io_service::work work_;

	boost::async_coro_queue<
		boost::circular_buffer_space_optimized<
			Message
		> > queue_;
	boost::shared_ptr<void> ctx_;
	boost::shared_ptr<void> socket_;
};

class ZmqPublisherClient
{
public:
	ZmqPublisherClient(boost::shared_ptr<ZmqPublisher> zmq_publisher, boost::asio::io_service &io, std::string channel_name, boost::function<void(std::string)> sender)
		: io_(io)
		, channel_name_(channel_name)
		, sender_(sender)
		, publisher_(zmq_publisher)
	{
	}

	void operator()(channel_identifier cid, avbotmsg msg, send_avchannel_message_t sender, boost::asio::yield_context yield_context)
	{
		std::stringstream ss;
		boost::property_tree::json_parser::write_json(ss, av_msg_make_json(cid, msg));
		publisher_->send(channel_name_, ss.str());
	}

private:
	boost::asio::io_service &io_;
	std::string channel_name_;
	boost::function<void (std::string)> sender_;

	boost::shared_ptr<ZmqPublisher> publisher_;
};

//ZmqPublisher ZmqPublisherClient::publisher_;

avbot_extension make_zmq_publisher(boost::asio::io_service &io, std::string channel_name, boost::function<void (std::string)> sender)
{
	static boost::shared_ptr<ZmqPublisher> zmq;
	if (!zmq)
	{
		zmq.reset(new ZmqPublisher);
	}
	return avbot_extension(channel_name, ZmqPublisherClient(zmq, io, channel_name, sender));
}
