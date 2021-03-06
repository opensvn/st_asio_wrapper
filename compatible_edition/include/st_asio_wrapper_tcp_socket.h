/*
 * st_asio_wrapper_tcp_socket.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * this class used at both client and server endpoint
 */

#ifndef ST_ASIO_WRAPPER_TCP_SOCKET_H_
#define ST_ASIO_WRAPPER_TCP_SOCKET_H_

#include <vector>

#include "st_asio_wrapper_socket.h"

#ifndef ST_ASIO_GRACEFUL_SHUTDOWN_MAX_DURATION
#define ST_ASIO_GRACEFUL_SHUTDOWN_MAX_DURATION	5 //seconds, maximum duration while graceful shutdown
#elif ST_ASIO_GRACEFUL_SHUTDOWN_MAX_DURATION <= 0
	#error graceful shutdown duration must be bigger than zero.
#endif

namespace st_asio_wrapper
{

template <typename Socket, typename Packer, typename Unpacker,
	template<typename, typename> class InQueue, template<typename> class InContainer,
	template<typename, typename> class OutQueue, template<typename> class OutContainer>
class st_tcp_socket_base : public st_socket<Socket, Packer, Unpacker, typename Packer::msg_type, typename Unpacker::msg_type, InQueue, InContainer, OutQueue, OutContainer>
{
public:
	typedef typename Packer::msg_type in_msg_type;
	typedef typename Packer::msg_ctype in_msg_ctype;
	typedef typename Unpacker::msg_type out_msg_type;
	typedef typename Unpacker::msg_ctype out_msg_ctype;

protected:
	typedef st_socket<Socket, Packer, Unpacker, typename Packer::msg_type, typename Unpacker::msg_type, InQueue, InContainer, OutQueue, OutContainer> super;
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	enum shutdown_states {NONE, FORCE, GRACEFUL};

	st_tcp_socket_base(boost::asio::io_service& io_service_) : super(io_service_), unpacker_(boost::make_shared<Unpacker>()), shutdown_state(NONE) {}
	template<typename Arg>
	st_tcp_socket_base(boost::asio::io_service& io_service_, Arg& arg) : super(io_service_, arg), unpacker_(boost::make_shared<Unpacker>()), shutdown_state(NONE) {}

public:
	virtual bool obsoleted() {return !is_shutting_down() && super::obsoleted();}

	//reset all, be ensure that there's no any operations performed on this st_tcp_socket_base when invoke it
	void reset() {reset_state(); shutdown_state = NONE; super::reset();}
	void reset_state()
	{
		unpacker_->reset_state();
		super::reset_state();
	}

	bool is_shutting_down() const {return NONE != shutdown_state;}

	//get or change the unpacker at runtime
	//changing unpacker at runtime is not thread-safe, this operation can only be done in on_msg(), reset() or constructor, please pay special attention
	//we can resolve this defect via mutex, but i think it's not worth, because this feature is not frequently used
	boost::shared_ptr<i_unpacker<out_msg_type> > inner_unpacker() {return unpacker_;}
	boost::shared_ptr<const i_unpacker<out_msg_type> > inner_unpacker() const {return unpacker_;}
	void inner_unpacker(const boost::shared_ptr<i_unpacker<out_msg_type> >& _unpacker_) {unpacker_ = _unpacker_;}

	using super::send_msg;
	///////////////////////////////////////////////////
	//msg sending interface
	TCP_SEND_MSG(send_msg, false) //use the packer with native = false to pack the msgs
	TCP_SEND_MSG(send_native_msg, true) //use the packer with native = true to pack the msgs
	//guarantee send msg successfully even if can_overflow equal to false
	//success at here just means put the msg into st_tcp_socket_base's send buffer
	TCP_SAFE_SEND_MSG(safe_send_msg, send_msg)
	TCP_SAFE_SEND_MSG(safe_send_native_msg, send_native_msg)
	//msg sending interface
	///////////////////////////////////////////////////

protected:
	void force_shutdown() {if (FORCE != shutdown_state) shutdown();}
	bool graceful_shutdown(bool sync) //will block until shutdown success or time out if sync equal to true
	{
		if (is_shutting_down())
			return false;
		else
			shutdown_state = GRACEFUL;

		boost::system::error_code ec;
		ST_THIS lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
		if (ec) //graceful shutdown is impossible
		{
			shutdown();
			return false;
		}

		if (sync)
		{
			int loop_num = ST_ASIO_GRACEFUL_SHUTDOWN_MAX_DURATION * 100; //seconds to 10 milliseconds
			while (--loop_num >= 0 && is_shutting_down())
				boost::this_thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(10));
			if (loop_num < 0) //graceful shutdown is impossible
			{
				unified_out::info_out("failed to graceful shutdown within %d seconds", ST_ASIO_GRACEFUL_SHUTDOWN_MAX_DURATION);
				shutdown();
			}
		}

		return !sync;
	}

	//ascs::socket will guarantee not call this function in more than one thread concurrently.
	//return false if send buffer is empty or sending not allowed or io_service stopped
	virtual bool do_send_msg()
	{
		if (is_send_allowed() && !ST_THIS stopped() && !ST_THIS send_msg_buffer.empty())
		{
			std::vector<boost::asio::const_buffer> bufs;
			{
#ifdef ST_ASIO_WANT_MSG_SEND_NOTIFY
				const size_t max_send_size = 0;
#else
				const size_t max_send_size = boost::asio::detail::default_max_transfer_size;
#endif
				size_t size = 0;
				typename super::in_msg msg;
				BOOST_AUTO(end_time, statistic::local_time());

				typename super::in_container_type::lock_guard lock(ST_THIS send_msg_buffer);
				while (ST_THIS send_msg_buffer.try_dequeue_(msg))
				{
					ST_THIS stat.send_delay_sum += end_time - msg.begin_time;
					size += msg.size();
					last_send_msg.resize(last_send_msg.size() + 1);
					last_send_msg.back().swap(msg);
					bufs.push_back(boost::asio::buffer(last_send_msg.back().data(), last_send_msg.back().size()));
					if (size >= max_send_size)
						break;
				}
			}

			if (!bufs.empty())
			{
				last_send_msg.front().restart();
				boost::asio::async_write(ST_THIS next_layer(), bufs,
					ST_THIS make_handler_error_size(boost::bind(&st_tcp_socket_base::send_handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)));

				return true;
			}
		}

		return false;
	}

	virtual void do_recv_msg()
	{
		BOOST_AUTO(recv_buff, unpacker_->prepare_next_recv());
		assert(boost::asio::buffer_size(recv_buff) > 0);

		boost::asio::async_read(ST_THIS next_layer(), recv_buff,
			boost::bind(&i_unpacker<out_msg_type>::completion_condition, unpacker_, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred),
			ST_THIS make_handler_error_size(boost::bind(&st_tcp_socket_base::recv_handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)));
	}

	virtual bool is_send_allowed() {return !is_shutting_down() && super::is_send_allowed();}
	//can send data or not(just put into send buffer)

	//msg can not be unpacked
	//the link is still available, so don't need to shutdown this st_tcp_socket_base at both client and server endpoint
	virtual void on_unpack_error() = 0;

#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
	virtual bool on_msg(out_msg_type& msg) {unified_out::debug_out("recv(" ST_ASIO_SF "): %s", msg.size(), msg.data()); return true;}
#endif

	virtual bool on_msg_handle(out_msg_type& msg, bool link_down) {unified_out::debug_out("recv(" ST_ASIO_SF "): %s", msg.size(), msg.data()); return true;}

	void shutdown()
	{
		boost::unique_lock<boost::shared_mutex> lock(shutdown_mutex);

		shutdown_state = FORCE;
		ST_THIS stop_all_timer();
		ST_THIS close(); //must after stop_all_timer(), it's very important
		ST_THIS started_ = false;
//		reset_state();

		if (ST_THIS lowest_layer().is_open())
		{
			boost::system::error_code ec;
			ST_THIS lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		}
	}

private:
	void recv_handler(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (!ec && bytes_transferred > 0)
		{
			typename Unpacker::container_type temp_msg_can;
			bool unpack_ok = unpacker_->parse_msg(bytes_transferred, temp_msg_can);
			size_t msg_num = temp_msg_can.size();
			if (msg_num > 0)
			{
				ST_THIS stat.recv_msg_sum += msg_num;
				ST_THIS temp_msg_buffer.resize(ST_THIS temp_msg_buffer.size() + msg_num);
				BOOST_AUTO(op_iter, ST_THIS temp_msg_buffer.rbegin());
				for (BOOST_AUTO(iter, temp_msg_can.rbegin()); iter != temp_msg_can.rend();)
				{
					ST_THIS stat.recv_byte_sum += (++iter).base()->size();
					(++op_iter).base()->swap(*iter.base());
				}
			}
			ST_THIS handle_msg();

			if (!unpack_ok)
			{
				on_unpack_error();
				//reset unpacker's state after on_unpack_error(), so user can get the left half-baked msg in on_unpack_error()
				unpacker_->reset_state();
			}
		}
		else
			ST_THIS on_recv_error(ec);
	}

	void send_handler(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (!ec)
		{
			ST_THIS stat.send_time_sum += statistic::local_time() - last_send_msg.front().begin_time;
			ST_THIS stat.send_byte_sum += bytes_transferred;
			ST_THIS stat.send_msg_sum += last_send_msg.size();
#ifdef ST_ASIO_WANT_MSG_SEND_NOTIFY
			ST_THIS on_msg_send(last_send_msg.front());
#endif
#ifdef ST_ASIO_WANT_ALL_MSG_SEND_NOTIFY
			if (ST_THIS send_msg_buffer.empty())
				ST_THIS on_all_msg_send(last_send_msg.back());
#endif
		}
		else
			ST_THIS on_send_error(ec);
		last_send_msg.clear();

		if (ec)
			ST_THIS sending = false;
		else if (!do_send_msg()) //send msg sequentially, which means second sending only after first sending success
		{
			ST_THIS sending = false;
			if (!ST_THIS send_msg_buffer.empty())
				ST_THIS send_msg(); //just make sure no pending msgs
		}
	}

protected:
	boost::container::list<typename super::in_msg> last_send_msg;
	boost::shared_ptr<i_unpacker<out_msg_type> > unpacker_;
	shutdown_states shutdown_state;

	boost::shared_mutex shutdown_mutex;
};

} //namespace

#endif /* ST_ASIO_WRAPPER_TCP_SOCKET_H_ */
