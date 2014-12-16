
#include "avchannel.hpp"

bool avchannel::can_handle(channel_identifier channel_id)
{
    // 看是否属于自己频道
    auto found = std::find(std::begin(m_rooms), std::end(m_rooms), channel_id);
    return found != std::end(m_rooms);
}

void avchannel::handle_message(channel_identifier channel_id, avbotmsg msg, send_avbot_message_t send_avbot_message, boost::asio::yield_context yield_context)
{
    // 开始处理本频道消息
    for (auto room : m_rooms)
    {
        // 避免重复发送给自己
        if (room == channel_id)
            continue;

        send_avbot_message(room, msg, yield_context);
    }
}
