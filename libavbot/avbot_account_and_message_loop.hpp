#pragma once

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/atomic.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/scope_exit.hpp>
#include <boost/signals2.hpp>
#include <boost/foreach.hpp>
#include "avbot_accounts.hpp"

// 这个类用来执行
class avbot_account_and_message_loop : boost::noncopyable
{
public:
	avbot_account_and_message_loop();

	~avbot_account_and_message_loop();

	void add_account(const concepts::avbot_account & avbot_account)
	{
		// 为新帐号创建新的协程
		auto avbot_account_ptr = std::make_shared<concepts::avbot_account>(avbot_account);
		boost::asio::spawn(m_io_service, boost::bind(&avbot_account_and_message_loop::message_loop_coro, this, m_quit, avbot_account_ptr, _1));
	}

#if BOOST_ASIO_HAS_MOVE
	void add_account(concepts::avbot_account && avbot_account)
	{
		// 为新帐号创建新的协程
		auto avbot_account_ptr = std::make_shared<concepts::avbot_account>(avbot_account);
		boost::asio::spawn(m_io_service, boost::bind(&avbot_account_and_message_loop::message_loop_coro, this, m_quit, avbot_account_ptr, _1));
	}
#endif

private:

	void message_loop_coro(std::shared_ptr<boost::atomic<bool> > flag_quit, std::shared_ptr<concepts::avbot_account>, boost::asio::yield_context yield);

private:
	std::shared_ptr< boost::atomic<bool> > m_quit;
	std::vector<concepts::avbot_account*> m_accouts;

	boost::asio::io_service m_io_service;
public:
	typedef boost::property_tree::ptree av_message_tree;
	typedef boost::signals2::signal<void(av_message_tree, std::shared_ptr<concepts::avbot_account> , boost::asio::yield_context) > on_message_type;
	// 每当有消息的时候激发.
	on_message_type on_message;
};

class av_chatroom;
typedef std::shared_ptr<av_chatroom> av_chatroom_ptr;

struct av_channel : boost::noncopyable
{
	// 旗下的 rooms
	std::vector<av_chatroom_ptr> m_rooms;
public:
	typedef boost::property_tree::ptree av_message_tree;
	typedef boost::signals2::signal<void(av_message_tree) > on_message_type;

	// 作为频道消息触发
	on_message_type on_message;
};

/*
 * 这个用来管理 chat room
 *
 */
struct av_chatroom : boost::noncopyable
{
	std::string m_room_name;
	// 对room 所从属帐号的弱引用。
	std::vector<
		boost::weak_ptr<concepts::avbot_account>
	> m_accounts;

	//
	std::vector<
		boost::weak_ptr<av_channel>
	> m_parent_channels;

	bool is_this_room(const avbot_account_and_message_loop::av_message_tree & message)
	{
		if(m_accounts.front().lock()->protocol == message.get<std::string>("protocol"))
		{
			return message.get<std::string>("room") == m_room_name;
		}
		return false;
	}

	template<typename Handler>
	void  async_send_message(std::string message, Handler handler)
	{
		// 借机清理掉死掉的节点
		for(auto it = m_accounts.begin() ; it != m_accounts.end(); )
		{
			if(!it->lock())
			{
				m_accounts.erase(it++);
			}else
			{
				++it;
			}
		}

		// FIXME : 对全部节点执行轮转算法，以让消息通过不同的账户均衡发送.
		// 现在只是简单的，只使用第一个而已
		m_accounts.front().lock()->async_send_message(m_room_name, message, handler);
	}

	av_chatroom(std::string room_name, boost::weak_ptr<concepts::avbot_account> _account)
		: m_room_name(room_name)
	{
		m_accounts.push_back(_account);
	}

	void same_room_add_different_account(boost::weak_ptr<concepts::avbot_account> _account)
	{
		m_accounts.push_back(_account);
	}
};

template<typename Signature>
class all_invoker;

// 这是一个　helper 类，用于将多个回调合并为一个，直到多个回调都被调用，才合并回调
template<typename R, typename Arg1>
class all_invoker<R(Arg1)>
{
	int num_called;
	int num_to_called;
	boost::tuples::tuple<Arg1> m_args;
	boost::function<R(Arg1)> m_handler;

public:
	all_invoker()
		: num_called(0)
	{}

	void operator()(Arg1 arg1)
	{
		num_called ++;
		m_args = boost::make_tuple(arg1);

		if(m_handler && num_called == num_to_called)
		{
			m_handler(arg1);
		}
	}

	template<typename Handler>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(Handler, R(Arg1))
	barrier(int numtowait, Handler handler)
	{
		using namespace boost::asio;
		boost::asio::detail::async_result_init<Handler, R(Arg1)>
			init(BOOST_ASIO_MOVE_CAST(Handler)(handler));

		barrier_impl<
 			BOOST_ASIO_HANDLER_TYPE(Handler, R(Arg1))
		>(numtowait, init.handler);

		return init.result.get();
	}
private:
	template<typename Handler>
	void barrier_impl(int numtowait, Handler handler)
	{
		// wait until
		if( num_called == numtowait)
		{
			// 立即调用
			handler(boost::get<0>(m_args));
		}else
		{
			// 投递然后等待调用
			m_handler =  handler;
			num_to_called = numtowait;
		}
	}
};

/*
 * 频道管理器
 */
class av_channel_manager : boost::noncopyable
{
	/*
	 * 从　avbot_account_and_message_loop 获得的珍贵回调．
	 */
	void on_message_callback(boost::property_tree::ptree av_message_tree, concepts::avbot_account * from, boost::asio::yield_context yield)
	{
		boost::system::error_code ec;
		av_chatroom_ptr bingo_room_ptr;
		// 对消息首先执行检索，看有木有这个　room
		BOOST_FOREACH(auto & channel_ptr, m_channels)
		{
			BOOST_FOREACH(av_chatroom_ptr & room_ptr, channel_ptr->m_rooms)
			{
				// 检查是否是这个room
				if(room_ptr->is_this_room(av_message_tree))
				{
					bingo_room_ptr = room_ptr;
				}
			}
		}

		if(bingo_room_ptr)
		{
			int num_of_rooms = 0;
			all_invoker<void(boost::system::error_code)> fake_handler;
			// 遍历同　channel 的每一个 room ,　执行发送
			BOOST_FOREACH(auto & channel_ptr, bingo_room_ptr->m_parent_channels)
			{
				auto channel_ptr_locked = channel_ptr.lock();
				BOOST_FOREACH(av_chatroom_ptr & room_ptr, channel_ptr_locked->m_rooms)
				{
					if(room_ptr != bingo_room_ptr)
					{
						num_of_rooms ++;
						std::string message = "";
						// room_ptr->messag_formater(av_message_tree),
						// TODO, format the json message to plain line, and respect the user-defined
						// premble
						room_ptr->async_send_message(
							message,
							fake_handler
						);
					}
				}
			}
			// wait until all message got sended
			fake_handler.barrier(num_of_rooms, yield[ec]);
		}
		else
		{
			// 没有找到，看来要建立新的　room 了
			// TODO
		}
	}

	std::vector<
		std::shared_ptr<av_channel>
	> m_channels;

	std::vector<
		std::shared_ptr<av_chatroom>
	> m_unspecified_rooms;

	boost::signals2::signal<void()> on_new_room;
	boost::signals2::signal<void()> on_new_channel;
};
