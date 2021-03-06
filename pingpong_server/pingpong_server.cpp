
#include <iostream>

//configuration
#define ST_ASIO_SERVER_PORT		9527
#define ST_ASIO_ASYNC_ACCEPT_NUM	5
#define ST_ASIO_REUSE_OBJECT //use objects pool
//#define ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
#define ST_ASIO_MSG_BUFFER_SIZE 65536
#define ST_ASIO_INPUT_QUEUE non_lock_queue
//if pingpong_client only send message in on_msg() or on_msg_handle(), which means a responsive system, a real pingpong test,
//then, before pingpong_server send each message, the previous message has been sent to pingpong_client,
//so sending buffer will always be empty, which means we will never operate sending buffer concurrently, so need no locks.
//
//if pingpong_client send message in on_msg_send(), then using non_lock_queue as input queue in pingpong_server will lead
//undefined behavior, please note.
#define ST_ASIO_DEFAULT_UNPACKER stream_unpacker //non-protocol
//configuration

#include "../include/ext/st_asio_wrapper_server.h"
using namespace st_asio_wrapper;
using namespace st_asio_wrapper::ext;

#ifdef _MSC_VER
#define atoll _atoi64
#endif

#define QUIT_COMMAND	"quit"
#define LIST_STATUS		"status"

//about congestion control
//
//in 1.3, congestion control has been removed (no post_msg nor post_native_msg anymore), this is because
//without known the business (or logic), framework cannot always do congestion control properly.
//now, users should take the responsibility to do congestion control, there're two ways:
//
//1. for receiver, if you cannot handle msgs timely, which means the bottleneck is in your business,
//    you should open/close congestion control intermittently;
//   for sender, send msgs in on_msg_send() or use sending buffer limitation (like safe_send_msg(..., false)),
//    but must not in service threads, please note.
//
//2. for sender, if responses are available (like pingpong test), send msgs in on_msg()/on_msg_handle().
//    this will reduce IO throughput, because SOCKET's sliding window is not fully used, pleae note.
//
//pingpong_server chose method #1
//BTW, if pingpong_client chose method #2, then pingpong_server can work properly without any congestion control,
//which means pingpong_server can send msgs back with can_overflow parameter equal to true, and memory occupation
//will be under control.

class echo_socket : public st_server_socket
{
public:
	echo_socket(i_server& server_) : st_server_socket(server_) {}

protected:
	//msg handling: send the original msg back(echo server)
	//congestion control, method #1, the peer needs its own congestion control too.
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
	//this virtual function doesn't exists if ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER been defined
	virtual bool on_msg(out_msg_type& msg)
	{
		auto re = direct_send_msg(std::move(msg));
		if (!re)
			congestion_control(true);
			//cannot handle (send it back) this msg timely, begin congestion control
			//'msg' will be put into receiving buffer, and be dispatched via on_msg_handle() in the future

		return re;
	}

	virtual bool on_msg_handle(out_msg_type& msg, bool link_down)
	{
		auto re = direct_send_msg(std::move(msg));
		if (re)
			congestion_control(false);
			//successfully handled the only one msg in receiving buffer, end congestion control
			//subsequent msgs will be dispatched via on_msg() again.

		return re;
	}
#else
	//if we used receiving buffer, congestion control will become much simpler, like this:
	virtual bool on_msg_handle(out_msg_type& msg, bool link_down) {return direct_send_msg(std::move(msg));}
#endif
	//msg handling end
};

class echo_server : public st_server_base<echo_socket>
{
public:
	echo_server(st_service_pump& service_pump_) : st_server_base(service_pump_) {}

	statistic get_statistic()
	{
		statistic stat;
		do_something_to_all([&stat](object_ctype& item) {stat += item->get_statistic();});

		return stat;
	}

protected:
	virtual bool on_accept(object_ctype& client_ptr) {boost::asio::ip::tcp::no_delay option(true); client_ptr->lowest_layer().set_option(option); return true;}
};

int main(int argc, const char* argv[])
{
	printf("usage: %s [<service thread number=1> [<port=%d> [ip=0.0.0.0]]]\n", argv[0], ST_ASIO_SERVER_PORT);
	if (argc >= 2 && (0 == strcmp(argv[1], "--help") || 0 == strcmp(argv[1], "-h")))
		return 0;
	else
		puts("type " QUIT_COMMAND " to end.");

	st_service_pump sp;
	echo_server echo_server_(sp);

	if (argc > 3)
		echo_server_.set_server_addr(atoi(argv[2]), argv[3]);
	else if (argc > 2)
		echo_server_.set_server_addr(atoi(argv[2]));

	auto thread_num = 1;
	if (argc > 1)
		thread_num = std::min(16, std::max(thread_num, atoi(argv[1])));

	sp.start_service(thread_num);
	while(sp.is_running())
	{
		std::string str;
		std::cin >> str;
		if (QUIT_COMMAND == str)
			sp.stop_service();
		else if (LIST_STATUS == str)
		{
			printf("link #: " ST_ASIO_SF ", invalid links: " ST_ASIO_SF "\n", echo_server_.size(), echo_server_.invalid_object_size());
			puts("");
			puts(echo_server_.get_statistic().to_string().data());
		}
	}

	return 0;
}
