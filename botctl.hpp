#pragma once

#include "libavbot/avbot.hpp"

// 命令控制, 所有的协议都能享受的命令控制在这里实现.
// msg_sender 是一个函数, on_command 用它发送消息.
void on_bot_command(avbot::av_message_tree message, avbot & mybot);

void set_do_vc(std::function<void(std::string)>);
void set_do_vc();
