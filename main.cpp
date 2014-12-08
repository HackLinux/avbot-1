
/**
 * @file   main.cpp
 * @author microcai <microcaicai@gmail.com>
 *
 */

#ifdef _MSC_VER
#include <direct.h>
#else
#include <unistd.h>
#endif
#ifdef _WIN32
#include <Windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#endif
#include <string>
#include <algorithm>
#include <vector>
#include <signal.h>
#include <fstream>
#include <iterator>

#include <boost/regex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
namespace fs = boost::filesystem;
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/make_shared.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/noncopyable.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/locale.hpp>
#include <boost/signals2.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/preprocessor.hpp>
#include <locale.h>
#include <cstring>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <wchar.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <soci-sqlite3.h>
#include <boost-optional.h>
#include <boost-tuple.h>
#include <boost-gregorian-date.h>
#include <soci.h>

#include <avhttp.hpp>

#include "boost/stringencodings.hpp"
#include "boost/avloop.hpp"
#include <boost/invoke_wrapper.hpp>

#include "libavbot/avbot.hpp"
#include "libavlog/avlog.hpp"

#include "libwebqq/webqq.hpp"

#include "counter.hpp"

#include "botctl.hpp"
#include "input.hpp"
#include "avbot_vc_feed_input.hpp"
#include "rpc/server.hpp"

#include "extension/extension.hpp"
#include "deCAPTCHA/decaptcha.hpp"
#include "deCAPTCHA/deathbycaptcha_decoder.hpp"
#include "deCAPTCHA/channel_friend_decoder.hpp"
#include "deCAPTCHA/anticaptcha_decoder.hpp"
#include "deCAPTCHA/avplayer_free_decoder.hpp"
#include "deCAPTCHA/jsdati_decoder.hpp"
#include "deCAPTCHA/hydati_decoder.hpp"

#include "gui/avbotgui.hpp"

extern "C" void avbot_setup_seghandler();
extern "C" const char * avbot_version();
extern "C" const char * avbot_version_build_time();
extern "C" int playsound();

extern pt::ptree parse_cfg(std::istream_iterator<char> first, std::istream_iterator<char> last);

char * execpath;
avlog logfile;			// 用于记录日志文件.

static counter cnt;				// 用于统计发言信息.
static std::string progname;
static bool need_vc = false;
extern std::string preamble_qq_fmt, preamble_irc_fmt, preamble_xmpp_fmt;

#ifdef  _WIN32
static void wrappered_hander(boost::system::error_code ec, std::string str, boost::function<void(boost::system::error_code, std::string)> handler, std::shared_ptr< boost::function<void()> > closer)
{
	if (*closer)
	{
		(*closer)();
	}
	handler(ec, str);
}
#endif

static void channel_friend_decoder_vc_inputer(std::string vcimagebuffer, boost::function<void(boost::system::error_code, std::string)> handler, avbot_vc_feed_input &vcinput)
{
#ifdef  _WIN32
	std::shared_ptr< boost::function<void()> > closer(new boost::function<void()>);
	boost::invoke_wrapper::invoke_once<void( boost::system::error_code, std::string)> wraper( handler);
	boost::function<void(boost::system::error_code, std::string)> secondwrapper = boost::bind(wrappered_hander, _1, _2, wraper, closer);
#else
	boost::invoke_wrapper::invoke_once<void(boost::system::error_code, std::string)> secondwrapper(handler);
#endif
	vcinput.async_input_read_timeout(35, secondwrapper);
	set_do_vc(boost::bind(secondwrapper, boost::system::error_code(), _1));

#ifdef _WIN32
	// also fire up an input box and the the input there!
	*closer = async_input_box_get_input_with_image(vcinput.get_io_service(), vcimagebuffer, boost::bind(secondwrapper, _1, _2));
#endif // _WIN32
}

static void vc_code_decoded(boost::system::error_code ec, std::string provider,
	std::string vccode, boost::function<void()> reportbadvc, avbot & mybot)
{
	set_do_vc();
	need_vc = false;

	// 关闭 settings 对话框 ，如果还没关闭的话

	if (ec)
	{
		printf("\r");
		fflush(stdout);
		AVLOG_ERR << literal_to_localstr("解码出错，重登录 ...");
		mybot.broadcast_message("验证码有错，重新登录QQ");
		mybot.relogin_qq_account();
		return;
	}

	AVLOG_INFO << literal_to_localstr("使用 ") 	<<  utf8_to_local_encode(provider)
		<< literal_to_localstr(" 成功解码验证码!");

	if (provider == "IRC/XMPP 好友辅助验证码解码器")
		mybot.broadcast_message("验证码已输入");

	mybot.feed_login_verify_code(vccode, reportbadvc);
}

static void on_verify_code(std::string imgbuf, avbot & mybot, decaptcha::deCAPTCHA & decaptcha)
{
	AVLOG_INFO << "got vercode from TX, now try to auto resovle it ... ...";

	need_vc = true;
	// 保存文件.
	std::ofstream img("vercode.jpeg",
		std::ofstream::openmode(std::ofstream::binary | std::ofstream::out)
	);

	img.write(&imgbuf[0], imgbuf.length());
	img.close();

	decaptcha.async_decaptcha(
		imgbuf,
		boost::bind(&vc_code_decoded, _1, _2, _3, _4, boost::ref(mybot))
	);
}

static void stdin_feed_broadcast(avbot & mybot, std::string line_input)
{
	if (need_vc)
	{
		return;
	}

	boost::trim_right(line_input);

	if (line_input.size() > 1)
	{
		mybot.broadcast_message(
			boost::str(
				boost::format("来自 avbot 命令行的消息: %s")
				% line_input
			)
		);
	}
}


struct build_group_has_qq
{
	bool operator()(const std::string & str)
	{
		return str.substr(0, 3) == "qq:";
	}
};

static void build_group(std::string chanelmapstring, avbot & mybot)
{
	std::vector<std::string> gs;
	boost::split(gs, chanelmapstring, boost::is_any_of(";"));

	int m = 1;

	BOOST_FOREACH(std::string  pergroup, gs)
	{
		std::vector<std::string> groups;
		boost::split(groups, pergroup, boost::is_any_of(","));

		// 对于包含了 qq: 的那个组，组名就是 qq号码，而其他的嘛，组名使用 groupXXX
		std::vector<std::string>::iterator qqgroup = std::find_if(groups.begin(),
			groups.end(), build_group_has_qq());

		if (qqgroup != groups.end())
		{
			BOOST_FOREACH(std::string  c, groups)
			{
				mybot.add_to_channel((*qqgroup).substr(3), c);
			}
		}
		else
		{
			m ++;
			std::string group_name = std::string("group") + boost::lexical_cast<std::string>(m);
			BOOST_FOREACH(std::string  c, groups)
			{
				mybot.add_to_channel(group_name, c);
			}
		}
	}
}

static std::string imgurlformater(avbot::av_message_tree qqmessage, std::string baseurl)
{
	std::string cface = qqmessage.get<std::string>("name");

	return avhttp::detail::escape_path(
		boost::str(
			boost::format("%s/images/%s/%s")
			% baseurl
			% avbot::image_subdir_name(cface)
			% cface
		)
	);
}

static void avbot_log(avbot::av_message_tree message, avbot & mybot, soci::session & db)
{
	std::string linemessage;

	std::string curtime, protocol, nick;

	curtime = avlog::current_time();

	protocol = message.get<std::string>("protocol");
	// 首先是根据 nick 格式化
	if ( protocol != "mail")
	{
		std::string textonly;
		linemessage += message.get<std::string>("preamble", "");

		BOOST_FOREACH(const avbot::av_message_tree::value_type & v, message.get_child("message"))
		{
			if (v.first == "text")
			{
				textonly += v.second.data();
				linemessage += avlog::html_escape(v.second.data());
			}
			else if (v.first == "url")
			{
				linemessage += boost::str(
					boost::format("<a href=\"%s\">%s</a>")
					% v.second.data()
					% v.second.data()
				);
			}
			else if (v.first == "cface")
			{
				if (mybot.fetch_img)
				{
					std::string cface = v.second.get<std::string>("name");
					linemessage += boost::str(
						boost::format("<img src=\"../images/%s/%s\" />")
						% avbot::image_subdir_name(cface)
						% cface
					);
				}
				else
				{

					std::string url = v.second.get<std::string>("gchatpicurl");
					linemessage += boost::str(
						boost::format("\t\t<img src=\"%s\" />\r\n")
						% url
					);
				}
			}
			else if (v.first == "img")
			{
				linemessage += boost::str(
					boost::format("\t\t<img src=\"%s\" />\r\n")
					% v.second.data()
				);
			}
		}
		std::string channel_name = message.get<std::string>("channel", "");
		if(protocol != "rpc")
			nick = message.get<std::string>("who.nick", "");

		if (channel_name.empty())
		{
			// 全部记录?
			// TODO
		}
		else
		{
			long rowid = 0;
			{
			// log to database
			db << "insert into avlog (date, protocol, channel, nick, message)"
				" values (:date, :protocol, :channel, :nick, :message)"
				, soci::use(curtime)
				, soci::use(protocol)
				, soci::use(channel_name)
				, soci::use(nick)
				, soci::use(textonly);
			}

			if (db.get_backend_name() == "sqlite3")
			rowid =  sqlite_api::sqlite3_last_insert_rowid(
				dynamic_cast<soci::sqlite3_session_backend*>(db.get_backend())->conn_
			);

			// 如果最好的办法就是遍历组里的所有QQ群，都记录一次.
			avbot::av_chanels_t channelmap = mybot.get_channel_map(channel_name);

			// 如果没有Q群，诶，只好，嘻嘻.
			logfile.add_log(channel_name, linemessage, rowid);

			BOOST_FOREACH(std::string roomname, channelmap)
			{
				if (roomname.substr(0, 3) == "qq:")
				{
					// 避免重复记录.
					if (roomname.substr(3) != channel_name)
					{
						logfile.add_log(roomname.substr(3), linemessage, rowid);
					}
				}
			}
		}
	}
	else
	{
		// TODO ,  暂时不记录邮件吧！

// 		mybot.get_channel_name("room");
//
// 		linemessage  = boost::str(
// 			boost::format( "[QQ邮件]\n发件人:%s\n收件人:%s\n主题:%s\n\n%s" )
// 			% message.get<std::string>("from") % message.get<std::string>("to")
//			% message.get<std::string>("subject")
// 			% message.get_child("message").data()
// 		);
	}
}

static void init_database(soci::session & db)
{
	db.open(soci::sqlite3, "avlog.db");

	db <<
	"create table if not exists avlog ("
		"`date` TEXT not null, "
		"`protocol` TEXT not null default \"/\", "
		"`channel` TEXT not null, "
		"`nick` TEXT not null default \"\", "
		"`message` TEXT not null default \" \""
	");";
}

static void my_on_bot_command(avbot::av_message_tree message, avbot & mybot)
{
	try
	{
		std::string textmessage;

		try
		{
			textmessage = message.get<std::string>("newbee");
			// 如果没有发生异常, 那就是新人入群消息了, 嘻嘻.
			// 格式化为 .qqbot newbee XXX, 哼!
			std::string nick = message.get<std::string>("who.name");
			textmessage = boost::str(boost::format(".qqbot newbee %s") % nick);
			message.put("message.text", textmessage);
			message.put("op", 1);

			boost::delayedcallsec(
					mybot.get_io_service(),
					6,
					boost::bind(on_bot_command, message, boost::ref(mybot))
			);
			return;
		}
		catch (const boost::property_tree::ptree_bad_path &)
		{
			textmessage = message.get<std::string>("message.text");
		}

		on_bot_command(message, mybot);
	}
	catch (...)
	{}
}

#ifdef WIN32
int daemon(int nochdir, int noclose)
{
	// nothing...
	return -1;
}
#endif // WIN32

#include "fsconfig.ipp"

void sighandler(boost::asio::io_service & io)
{
	io.stop();
	std::cout << "Quiting..." << std::endl;
}

int main(int argc, char * argv[])
{
#ifdef _WIN32
	::InitCommonControls();
#endif

	std::string jsdati_username, jsdati_password;
	std::string hydati_key;
	std::string deathbycaptcha_username, deathbycaptcha_password;
	//http://api.dbcapi.me/in.php
	//http://antigate.com/in.php
	std::string antigate_key, antigate_host;
	bool use_avplayer_free_vercode_decoder(false);
	bool no_persistent_db(false);
	std::string weblogbaseurl;

	fs::path run_root; // 运行时的根
	fs::path account_settings;

	unsigned rpcport;

	boost::asio::io_service io_service;

	avbot mybot(io_service);

	progname = fs::basename(argv[0]);

#ifdef _WIN32
	setlocale(LC_ALL, "zh");
#else
	setlocale(LC_ALL, "");
#endif

	po::variables_map vm;
	po::options_description desc("qqbot options");
	desc.add_options()
	("version,v", 	"output version")
	("help,h", 	"produce help message")
	("daemon,d", 	"go to background")
	("nostdin", 	"don't read from stdin (systemd daemon mode)")
#if defined(WIN32) || defined(WITH_QT_GUI)
	("gui,g",	 	"pop up settings dialog")
#endif

	("config,c", po::value<fs::path>(&account_settings),
		"path to account file")
	("logdir,d", po::value<fs::path>(&run_root),
		"path to logs")
	("nopersistent,s", po::value<bool>(&no_persistent_db),
		"do not use persistent database file to increase security.")

	("jsdati_username", po::value<std::string>(&jsdati_username),
		literal_to_localstr("联众打码服务账户").c_str())
	("jsdati_password", po::value<std::string>(&jsdati_password),
		literal_to_localstr("联众打码服务密码").c_str())

	("hydati_key", po::value<std::string>(&hydati_key),
		literal_to_localstr("慧眼答题服务key").c_str())

	("deathbycaptcha_username", po::value<std::string>(&deathbycaptcha_username),
		literal_to_localstr("阿三解码服务账户").c_str())
	("deathbycaptcha_password", po::value<std::string>(&deathbycaptcha_password),
		literal_to_localstr("阿三解码服务密码").c_str())

	("antigate_key", po::value<std::string>(&antigate_key),
		literal_to_localstr("antigate解码服务key").c_str())
	("antigate_host", po::value<std::string>(&antigate_host)->default_value("http://antigate.com/"),
		literal_to_localstr("antigate解码服务器地址").c_str())

	("use_avplayer_free_vercode_decoder", po::value<bool>(&use_avplayer_free_vercode_decoder),
		"ask microcai for permission")

	("localimage", po::value<bool>(&(mybot.fetch_img))->default_value(true),
		"fetch qq image to local disk and store it there")
	("weblogbaseurl", po::value<std::string>(&(weblogbaseurl)),
		"base url for weblog serving")
	("rpcport",	po::value<unsigned>(&rpcport)->default_value(6176),
		"run rpc server on port 6176")

	("preambleqq", po::value<std::string>(&preamble_qq_fmt)->default_value(literal_to_localstr("qq(%a): ")),
		literal_to_localstr("为QQ设置的发言前缀, 默认是 qq(%a): ").c_str())
	("preambleirc", po::value<std::string>(&preamble_irc_fmt)->default_value(literal_to_localstr("%a 说: ")),
		literal_to_localstr("为IRC设置的发言前缀, 默认是 %a 说: ").c_str())
	("preamblexmpp", po::value<std::string>(&preamble_xmpp_fmt)->default_value(literal_to_localstr("(%a): ")),
		literal_to_localstr(
			"为XMPP设置的发言前缀, 默认是 (%a): \n\n "
			"前缀里的含义 \n"
			"\t %a 为自动选择\n\t %q 为QQ号码\n\t %n 为昵称\n\t %c 为群名片 \n"
			"\t %r为房间名(群号, XMPP房名, IRC频道名) \n"
			"可以包含多个, 例如想记录QQ号码的可以使用 qq(%a, %q)说: \n"
			"注意在shell下可能需要使用\\(来转义(\n配置文件无此问题 \n\n").c_str())
	;

	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		std::cerr <<  desc <<  std::endl;
		return 1;
	}

	if (vm.count("version"))
	{
		printf("qqbot version %s (%s %s) \n", avbot_version() , __DATE__, __TIME__);
		exit(EXIT_SUCCESS);
	}

#ifdef WIN32
	// 从 windows 控制台输入的能有啥好编码，转到utf8吧.
	preamble_qq_fmt = ansi_utf8(preamble_qq_fmt);
	preamble_irc_fmt = ansi_utf8(preamble_irc_fmt);
	preamble_xmpp_fmt = ansi_utf8(preamble_xmpp_fmt);
#endif

#ifndef _WIN32
	// 设置到中国的时区，否则 qq 消息时间不对啊.
	setenv("TZ", "Asia/Shanghai", 1);
# endif

	// 设置 BackTrace
	avbot_setup_seghandler();


	{
#ifndef _WIN32
		std::string env_oldpwd = fs::complete(fs::current_path()).string();
		setenv("O_PWD", env_oldpwd.c_str(), 1);
# else
		std::string env_oldpwd = std::string("O_PWD=") + fs::complete(fs::current_path()).string();
		putenv((char *)env_oldpwd.c_str());
#endif

	}

	// 设置日志自动记录目录.
	if (!run_root.empty())
	{
		logfile.log_path(run_root.string());
#ifdef _WIN32
		SetCurrentDirectoryW(avhttp::detail::utf8_wide(logdir).c_str());
#else
		chdir(run_root.c_str());
#endif // _WIN32
	}

	soci::session avlogdb;

	init_database(avlogdb);

	decaptcha::deCAPTCHA decaptcha_agent(io_service);

	avbot_vc_feed_input vcinput(io_service);

	// 连接到 std input
	connect_stdinput(
		boost::bind(&avbot_vc_feed_input::call_this_to_feed_line, &vcinput, _1)
	);

	if (!hydati_key.empty())
	{
		decaptcha_agent.add_decoder(
			decaptcha::decoder::hydati_decoder(
				io_service, hydati_key
			)
		);
	}

	if (!jsdati_username.empty() && !jsdati_password.empty())
	{
		decaptcha_agent.add_decoder(
			decaptcha::decoder::jsdati_decoder(
				io_service, jsdati_username, jsdati_password
			)
		);
	}

	if (!deathbycaptcha_username.empty() && !deathbycaptcha_password.empty())
	{
		decaptcha_agent.add_decoder(
			decaptcha::decoder::deathbycaptcha_decoder(
				io_service, deathbycaptcha_username, deathbycaptcha_password
			)
		);
	}

	if (!antigate_key.empty())
	{
		decaptcha_agent.add_decoder(
			decaptcha::decoder::anticaptcha_decoder(io_service, antigate_key, antigate_host)
		);
	}

	if (use_avplayer_free_vercode_decoder)
	{
		decaptcha_agent.add_decoder(
			decaptcha::decoder::avplayer_free_decoder(io_service)
		);
	}

	decaptcha_agent.add_decoder(
		decaptcha::decoder::channel_friend_decoder(
			io_service,
			boost::bind(&avbot::broadcast_message, &mybot, _1),
			boost::bind(&channel_friend_decoder_vc_inputer, _1, _2, boost::ref(vcinput))
		)
	);

	mybot.preamble_irc_fmt = preamble_irc_fmt;
	mybot.preamble_qq_fmt = preamble_qq_fmt;
	mybot.preamble_xmpp_fmt = preamble_xmpp_fmt;

	mybot.signal_new_channel.connect(
		boost::bind(
			new_channel_set_extension,
			boost::ref(io_service),
			boost::ref(mybot),
			_1
		)
	);

	// 解析 accounts 文件, 设置帐号
	pt::ptree accounts_settings;{
	std::ifstream accounts_file_stream;
	accounts_file_stream.open(account_settings.string().c_str(), std::ifstream::in);
	std::istream_iterator<char> accounts_file_begin(accounts_file_stream);
	std::istream_iterator<char> accounts_file_end;

	accounts_settings = parse_cfg(accounts_file_begin, accounts_file_end);}

	//

	// TODO 遍历文件夹, 设置 channel

	// 记录到日志.
	mybot.on_message.connect(
		boost::bind(avbot_log, _1, boost::ref(mybot), boost::ref(avlogdb))
	);
	// 开启 bot 控制.
	mybot.on_message.connect(
		boost::bind(my_on_bot_command, _1, boost::ref(mybot))
	);

	boost::asio::io_service::work work(io_service);

	if (!vm.count("daemon") && !vm.count("nostdin"))
	{
		start_stdinput(io_service);
		connect_stdinput(boost::bind(stdin_feed_broadcast , boost::ref(mybot), _1));
	}

	if (!weblogbaseurl.empty())
	{
		mybot.m_urlformater = boost::bind(&imgurlformater, _1, weblogbaseurl);
	}

	if (rpcport > 0)
	{
		if (!avbot_start_rpc(io_service, rpcport, mybot, avlogdb))
		{
			AVLOG_WARN <<  "bind to port " <<  rpcport <<  " failed!";
			AVLOG_WARN <<  "Did you happened to already run an avbot? ";
			AVLOG_WARN <<  "Now avbot will run without RPC support. ";
		};
	}

	avhttp::http_stream s(io_service);
	s.async_open(
		"https://avlog.avplayer.org/cache/tj.php",
		boost::bind(&avhttp::http_stream::close, &s)
	);

	avloop_idle_post(io_service, playsound);

	boost::asio::signal_set terminator_signal(io_service);
	terminator_signal.add(SIGINT);
	terminator_signal.add(SIGTERM);
#if defined(SIGQUIT)
	terminator_signal.add(SIGQUIT);
#endif // defined(SIGQUIT)
	terminator_signal.async_wait(boost::bind(&sighandler, boost::ref(io_service)));

#ifdef HAVE_SYSTEMD
	// watchdog timer logic

	// check WATCHDOG_USEC environment variable
	uint64_t watchdog_usec;
	if(sd_watchdog_enabled(1, &watchdog_usec))
	{
		mybot.on_message.connect([](avbot::av_message_tree)
		{
			sd_notify(0, "WATCHDOG=1");
		});
	};

	avloop_idle_post(io_service, boost::bind(&sd_notify,0, "READY=1"));
#endif

	avloop_run_gui(io_service);
	return 0;
}
