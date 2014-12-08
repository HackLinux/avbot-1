
/************************************************************************
  protocol adapters, 将 libwebqq/libirc/libxmpp
  弄成兼容 avbot_accounts 接口
  目前这种做法只是暂时的，最终还是要把这几个类全改写了
 ************************************************************************/
#include <boost/system/error_code.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/coroutine.hpp>
#include <vector>
#include <string>
#include <irc.hpp>
#include <libwebqq/webqq.hpp>
#include <libavbot/avbot_accounts.hpp>

std::string preamble_qq_fmt, preamble_irc_fmt, preamble_xmpp_fmt;

static std::string	preamble_formater(std::string preamble_irc_fmt, irc::irc_msg pmsg)
{
	// 格式化神器, 哦耶.
	// 获取格式化描述字符串.
	std::string preamble = preamble_irc_fmt;

	// 支持的格式化类型有 %u UID,  %q QQ号, %n 昵称,  %c 群名片 %a 自动 %r irc 房间.
	// 默认为 qq(%a) 说:.
	boost::replace_all(preamble, "%a", pmsg.whom);
	boost::replace_all(preamble, "%r", pmsg.from);
	boost::replace_all(preamble, "%n", pmsg.whom);
	return preamble;
}

class example
{
public:
	void async_login(boost::function<void(boost::system::error_code)> handler) {};
	void async_recv_message(boost::function<void(boost::system::error_code, boost::property_tree::ptree)> handler) {};
	void async_send_message(std::string target, std::string message, boost::function<void(boost::system::error_code)> handler) {};
	void async_join_group(std::string groupname, boost::function<void(boost::system::error_code)> handler){};
};

class avwebqq
{
	template<typename Handler>
	class async_login_op
	{
		avwebqq * _caller;


		void operator()(boost::system::error_code ec)
		{
			BOOST_ASIO_CORO_REENTER(this)
			{
				BOOST_ASIO_CORO_YIELD _caller->m_webqq.async_login(*this);
			}
		}
	};

	template<typename>
	friend class async_login_op;

	webqq::webqq m_webqq;
public:
	template<typename Handler>
	void async_login(Handler handler)
	{
		// 登录 qq, 调用 deCAPTCHA
		async_login_op<Handler>()(handler);
	}

};

class avxmpp{};

class avirc
{
	irc::client m_irc;
public:
	template<typename Handler>
	void async_login(Handler handler)
	{
		// 执行异步登录
		boost::system::error_code ec;
		// 假装已经登录了
		handler(ec);
	}

	template<typename Handler>
	void async_recv_message(Handler handler)
	{
		// 从 irc 接收消息
		m_handlers.push_back(handler);

		m_irc.on_privmsg_message(boost::bind(&avirc::callback_on_irc_message, this, _1));
	}

	template<typename Handler>
	void async_join_group(std::string groupname, Handler handler)
	{
		boost::system::error_code ec;
		m_irc.join(groupname);
		handler(ec);
	}

	void callback_on_irc_message(irc::irc_msg pMsg)
	{
		using boost::property_tree::ptree;
		// formate irc message to JSON and call on_message

		ptree message;
		message.put("protocol", "irc");
		message.put("room", pMsg.from.substr(1));
		message.put("who.nick", pMsg.whom);
//		message.put("channel", get_channel_name(std::string("irc:") + pMsg.from.substr(1)));
		message.put("preamble", preamble_formater(preamble_irc_fmt, pMsg));

		ptree textmsg;
		textmsg.add("text", pMsg.msg);
		message.add_child("message", textmsg);

		if (!m_handlers.empty())
		{
			boost::system::error_code ec;
			m_handlers.front()(ec, message);
			m_handlers.erase(m_handlers.begin());
		}
	}
	std::vector<
		boost::function<void(boost::system::error_code, boost::property_tree::ptree)>
	>m_handlers;
};

namespace concepts{
namespace implementation{

template<>
class avbot_account_adapter<webqq::webqq> : public avbot_account_indrector
{
	webqq::webqq m_webqq;
public:

#ifdef BOOST_ASIO_HAS_MOVE
	avbot_account_adapter(webqq::webqq && _webqq)
		:m_webqq(_webqq)
	{
	}
#endif // BOOST_ASIO_HAS_MOVE

	avbot_account_adapter(const webqq::webqq & _webqq)
		:m_webqq(_webqq)
	{
	}
private:

	virtual void async_login(boost::function<void(boost::system::error_code)> handler)
	{
	}
};


}}

