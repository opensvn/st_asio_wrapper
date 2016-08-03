/*
 * st_asio_wrapper_packer.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * packer base class
 */

#ifndef ST_ASIO_WRAPPER_PACKER_H_
#define ST_ASIO_WRAPPER_PACKER_H_

#include "st_asio_wrapper_ext.h"

#ifdef ST_ASIO_HUGE_MSG
#define ST_ASIO_HEAD_TYPE	uint32_t
#define ST_ASIO_HEAD_H2N	htonl
#else
#define ST_ASIO_HEAD_TYPE	uint16_t
#define ST_ASIO_HEAD_H2N	htons
#endif
#define ST_ASIO_HEAD_LEN	(sizeof(ST_ASIO_HEAD_TYPE))

namespace st_asio_wrapper { namespace ext {

class packer_helper
{
public:
	//return (size_t) -1 means length exceeded the ST_ASIO_MSG_BUFFER_SIZE
	static size_t msg_size_check(size_t pre_len, const char* const pstr[], const size_t len[], size_t num)
	{
		if (nullptr == pstr || nullptr == len)
			return -1;

		auto total_len = pre_len;
		auto last_total_len = total_len;
		for (size_t i = 0; i < num; ++i)
			if (nullptr != pstr[i])
			{
				total_len += len[i];
				if (last_total_len > total_len || total_len > ST_ASIO_MSG_BUFFER_SIZE) //overflow
				{
					unified_out::error_out("pack msg error: length exceeded the ST_ASIO_MSG_BUFFER_SIZE!");
					return -1;
				}
				last_total_len = total_len;
			}

		return total_len;
	}
};

//protocol: length + body
class packer : public i_packer<std::string>
{
public:
	static size_t get_max_msg_size() {return ST_ASIO_MSG_BUFFER_SIZE - ST_ASIO_HEAD_LEN;}

	using i_packer<msg_type>::pack_msg;
	virtual msg_type pack_msg(const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		msg_type msg;
		auto pre_len = native ? 0 : ST_ASIO_HEAD_LEN;
		auto total_len = packer_helper::msg_size_check(pre_len, pstr, len, num);
		if ((size_t) -1 == total_len)
			return msg;
		else if (total_len > pre_len)
		{
			if (!native)
			{
				auto head_len = (ST_ASIO_HEAD_TYPE) total_len;
				if (total_len != head_len)
				{
					unified_out::error_out("pack msg error: length exceeded the header's range!");
					return msg;
				}

				head_len = ST_ASIO_HEAD_H2N(head_len);
				msg.reserve(total_len);
				msg.append((const char*) &head_len, ST_ASIO_HEAD_LEN);
			}
			else
				msg.reserve(total_len);

			for (size_t i = 0; i < num; ++i)
				if (nullptr != pstr[i])
					msg.append(pstr[i], len[i]);
		} //if (total_len > pre_len)

		return msg;
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(std::next(msg.data(), ST_ASIO_HEAD_LEN));}
	virtual const char* raw_data(msg_ctype& msg) const {return std::next(msg.data(), ST_ASIO_HEAD_LEN);}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - ST_ASIO_HEAD_LEN;}
};

//protocol: length + body
//use memory pool
class pooled_packer : public i_packer<shared_buffer<memory_pool::raw_buffer_type>>
{
public:
	static size_t get_max_msg_size() {return ST_ASIO_MSG_BUFFER_SIZE - ST_ASIO_HEAD_LEN;}

	pooled_packer() : pool(nullptr) {}
	void mem_pool(memory_pool& _pool) {pool = &_pool;}
	memory_pool& mem_pool() {return *pool;}

	using i_packer<msg_type>::pack_msg;
	virtual msg_type pack_msg(const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		msg_type msg;
		auto pre_len = native ? 0 : ST_ASIO_HEAD_LEN;
		auto total_len = packer_helper::msg_size_check(pre_len, pstr, len, num);
		if ((size_t) -1 == total_len)
			return msg;
		else if (total_len > pre_len)
		{
			if (!native)
			{
				auto head_len = (ST_ASIO_HEAD_TYPE) total_len;
				if (total_len != head_len)
				{
					unified_out::error_out("pack msg error: length exceeded the header's range!");
					return msg;
				}

				msg.raw_buffer(pool->checkout(total_len));
				head_len = ST_ASIO_HEAD_H2N(head_len);
				memcpy(msg.raw_buffer()->data(), (const char*) &head_len, ST_ASIO_HEAD_LEN);
			}
			else
				msg.raw_buffer(pool->checkout(total_len));

			total_len = pre_len;
			for (size_t i = 0; i < num; ++i)
				if (nullptr != pstr[i])
				{
					memcpy(std::next(msg.raw_buffer()->data(), total_len), pstr[i], len[i]);
					total_len += len[i];
				}
		} //if (total_len > pre_len)

		return msg;
	}

	virtual char* raw_data(msg_type& msg) const {return std::next(msg.raw_buffer()->data(), ST_ASIO_HEAD_LEN);}
	virtual const char* raw_data(msg_ctype& msg) const {return std::next(msg.data(), ST_ASIO_HEAD_LEN);}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - ST_ASIO_HEAD_LEN;}

protected:
	memory_pool* pool;
};

//protocol: length + body
class replaceable_packer : public i_packer<replaceable_buffer>
{
public:
	using i_packer<msg_type>::pack_msg;
	virtual msg_type pack_msg(const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		packer p;
		auto msg = p.pack_msg(pstr, len, num, native);
		auto com = boost::make_shared<string_buffer>();
		com->swap(msg);

		return msg_type(com);
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(std::next(msg.data(), ST_ASIO_HEAD_LEN));}
	virtual const char* raw_data(msg_ctype& msg) const {return std::next(msg.data(), ST_ASIO_HEAD_LEN);}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - ST_ASIO_HEAD_LEN;}
};

//protocol: [prefix] + body + suffix
class prefix_suffix_packer : public i_packer<std::string>
{
public:
	void prefix_suffix(const std::string& prefix, const std::string& suffix) {assert(!suffix.empty() && prefix.size() + suffix.size() < ST_ASIO_MSG_BUFFER_SIZE); _prefix = prefix;  _suffix = suffix;}
	const std::string& prefix() const {return _prefix;}
	const std::string& suffix() const {return _suffix;}

public:
	using i_packer<msg_type>::pack_msg;
	virtual msg_type pack_msg(const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		msg_type msg;
		auto pre_len = native ? 0 : _prefix.size() + _suffix.size();
		auto total_len = packer_helper::msg_size_check(pre_len, pstr, len, num);
		if ((size_t) -1 == total_len)
			return msg;
		else if (total_len > pre_len)
		{
			msg.reserve(total_len);
			if (!native)
				msg.append(_prefix);
			for (size_t i = 0; i < num; ++i)
				if (nullptr != pstr[i])
					msg.append(pstr[i], len[i]);
			if (!native)
				msg.append(_suffix);
		} //if (total_len > pre_len)

		return msg;
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(std::next(msg.data(), _prefix.size()));}
	virtual const char* raw_data(msg_ctype& msg) const {return std::next(msg.data(), _prefix.size());}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - _prefix.size() - _suffix.size();}

private:
	std::string _prefix, _suffix;
};

//protocol: stream (non-protocol)
//use memory pool
class pooled_stream_packer : public i_packer<shared_buffer<memory_pool::raw_buffer_type>>
{
public:
	pooled_stream_packer() : pool(nullptr) {}
	void mem_pool(memory_pool& _pool) {pool = &_pool;}
	memory_pool& mem_pool() {return *pool;}

	using i_packer<msg_type>::pack_msg;
	virtual msg_type pack_msg(const char* const pstr[], const size_t len[], size_t num, bool native = false) //native will not take effect
	{
		msg_type msg;
		auto total_len = packer_helper::msg_size_check(0, pstr, len, num);
		if ((size_t) -1 == total_len)
			return msg;
		else if (total_len > 0)
		{
			msg.raw_buffer(pool->checkout(total_len));

			total_len = 0;
			for (size_t i = 0; i < num; ++i)
				if (nullptr != pstr[i])
				{
					memcpy(std::next(msg.raw_buffer()->data(), total_len), pstr[i], len[i]);
					total_len += len[i];
				}
		} //if (total_len > 0)

		return msg;
	}

	virtual char* raw_data(msg_type& msg) const {return msg.raw_buffer()->data();}
	virtual const char* raw_data(msg_ctype& msg) const {return msg.data();}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size();}

protected:
	memory_pool* pool;
};

}} //namespace

#endif /* ST_ASIO_WRAPPER_PACKER_H_ */