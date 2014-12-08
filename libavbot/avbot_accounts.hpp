#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio.hpp>
#include <string>

namespace concepts{

class avbot_account;

namespace implementation{

	class avbot_account_indrector
	{
		friend class concepts::avbot_account;
		virtual void async_login(boost::function<void(boost::system::error_code)> handler) = 0;
		virtual void async_recv_message(boost::function<void(boost::system::error_code, boost::property_tree::ptree)> handler) = 0;
		virtual void async_send_message(std::string target, std::string message, boost::function<void(boost::system::error_code)> handler) = 0;
		virtual void async_join_group(std::string groupname, boost::function<void(boost::system::error_code)> handler) = 0;
	public:
		virtual ~avbot_account_indrector(){}
	};

	template<typename T>
	class avbot_account_adapter : public avbot_account_indrector
	{
		friend class avbot_account;

		T m_real_avbot_account;
	public:
#ifdef BOOST_ASIO_HAS_MOVE
		avbot_account_adapter(T && wrapee)
			: m_real_avbot_account(wrapee)
		{
		}
#endif // !BOOST_ASIO_HAS_MOVE
		avbot_account_adapter(const T &wrapee)
			: m_real_avbot_account(wrapee)
		{
		}
	private:
		avbot_account_adapter(const avbot_account_adapter & other)
		{
		}
	private:
		virtual void async_login(boost::function<void(boost::system::error_code)> handler)
		{
			m_real_avbot_account.async_login(handler);
		}

		virtual void async_recv_message(boost::function<void(boost::system::error_code, boost::property_tree::ptree)> handler)
		{
			m_real_avbot_account.async_recv_message(handler);
		}

		virtual void async_send_message(std::string target, std::string message, boost::function<void(boost::system::error_code)> handler)
		{
			m_real_avbot_account.async_send_message(target, message, handler);
		}

		virtual void async_join_group(std::string groupname, boost::function<void(boost::system::error_code)> handler)
		{
			m_real_avbot_account.async_join_group(groupname, handler);
		}
	};

}

// copyable and movable, so it can be put into STL container
class avbot_account
{
	std::shared_ptr<implementation::avbot_account_indrector> _impl;
public:
	template<typename Handler>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code))
		async_login(BOOST_ASIO_MOVE_ARG(Handler) handler)
	{
		using namespace boost::asio;
		boost::asio::detail::async_result_init<Handler, void(boost::system::error_code)>
			init(BOOST_ASIO_MOVE_CAST(Handler)(handler));

		_impl->async_login(init.handler);

		return init.result.get();
	}

	template<typename Handler>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code, boost::property_tree::ptree))
		async_recv_message(BOOST_ASIO_MOVE_ARG(Handler) handler)
	{
		using namespace boost::asio;
		boost::asio::detail::async_result_init<Handler, void(boost::system::error_code, boost::property_tree::ptree)>
			init(BOOST_ASIO_MOVE_CAST(Handler)(handler));

		_impl->async_recv_message(init.handler);

		return init.result.get();
	}

	template<typename Handler>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code))
		async_send_message(std::string target, std::string message, BOOST_ASIO_MOVE_ARG(Handler) handler)
	{
		using namespace boost::asio;
		boost::asio::detail::async_result_init<Handler, void(boost::system::error_code)>
			init(BOOST_ASIO_MOVE_CAST(Handler)(handler));

		_impl->async_send_message(target, message, init.handler);

		return init.result.get();
	}

	template<typename Handler>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(Handler, void(boost::system::error_code))
		async_join_group(std::string groupname, Handler handler)
	{
		using namespace boost::asio;
		boost::asio::detail::async_result_init<Handler, void(boost::system::error_code)>
			init(BOOST_ASIO_MOVE_CAST(Handler)(handler));

		_impl->async_join_group(groupname, init.handler);

		return init.result.get();
	}

	static bool always_false(boost::system::error_code)
	{
		return false;
	}

	avbot_account(std::string _protocol)
		:protocol(_protocol)
	{
		is_error_fatal = always_false;
	}

	avbot_account(const avbot_account & other)
		:protocol(other.protocol)
	{
		_impl = other._impl;
		is_error_fatal = other.is_error_fatal;
	}

	template<typename T>
	avbot_account(std::string _protocol, const T & wrapee)
		:protocol(_protocol)
	{
		is_error_fatal = always_false;
		_impl.reset(new implementation::avbot_account_adapter<typename boost::remove_reference<T>::type>(wrapee));
	}

#if BOOST_ASIO_HAS_MOVE

	avbot_account(avbot_account && other)
		:protocol(other.protocol)
	{
		_impl = std::move(other._impl);
		is_error_fatal = std::move(other.is_error_fatal);
	}

	template<typename T>
	avbot_account(std::string _protocol, T && wrapee)
		:protocol(_protocol)
	{
		is_error_fatal = always_false;
		_impl.reset(new implementation::avbot_account_adapter<typename boost::remove_reference<T>::type>(wrapee));
	}

#endif

	// 致命错误不能恢复，只能禁用这个 account 。
	boost::function<bool(boost::system::error_code)> is_error_fatal;
	std::string protocol;
};

}
