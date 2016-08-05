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
#define ST_ASIO_HEAD_TYPE	boost::uint32_t
#define ST_ASIO_HEAD_H2N	htonl
#else
#define ST_ASIO_HEAD_TYPE	boost::uint16_t
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
		if (NULL == pstr || NULL == len)
			return -1;

		size_t total_len = pre_len;
		size_t last_total_len = total_len;
		for (size_t i = 0; i < num; ++i)
			if (NULL != pstr[i])
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
	virtual bool pack_msg(msg_type& msg, const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		msg.clear();
		size_t pre_len = native ? 0 : ST_ASIO_HEAD_LEN;
		size_t total_len = packer_helper::msg_size_check(pre_len, pstr, len, num);
		if ((size_t) -1 == total_len)
			return false;
		else if (total_len > pre_len)
		{
			if (!native)
			{
				ST_ASIO_HEAD_TYPE head_len = (ST_ASIO_HEAD_TYPE) total_len;
				if (total_len != head_len)
				{
					unified_out::error_out("pack msg error: length exceeded the header's range!");
					return false;
				}

				head_len = ST_ASIO_HEAD_H2N(head_len);
				msg.reserve(total_len);
				msg.append((const char*) &head_len, ST_ASIO_HEAD_LEN);
			}
			else
				msg.reserve(total_len);

			for (size_t i = 0; i < num; ++i)
				if (NULL != pstr[i])
					msg.append(pstr[i], len[i]);
		} //if (total_len > pre_len)

		return true;
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(boost::next(msg.data(), ST_ASIO_HEAD_LEN));}
	virtual const char* raw_data(msg_ctype& msg) const {return boost::next(msg.data(), ST_ASIO_HEAD_LEN);}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - ST_ASIO_HEAD_LEN;}
};

//protocol: length + body
class replaceable_packer : public i_packer<replaceable_buffer>
{
public:
	using i_packer<msg_type>::pack_msg;
	virtual bool pack_msg(msg_type& msg, const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		packer::msg_type str;
		if (packer().pack_msg(str, pstr, len, num, native))
		{
			BOOST_AUTO(raw_msg, new string_buffer());
			raw_msg->swap(str);
			msg.raw_buffer(raw_msg);

			return true;
		}

		return false;
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(boost::next(msg.data(), ST_ASIO_HEAD_LEN));}
	virtual const char* raw_data(msg_ctype& msg) const {return boost::next(msg.data(), ST_ASIO_HEAD_LEN);}
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
	virtual bool pack_msg(msg_type& msg, const char* const pstr[], const size_t len[], size_t num, bool native = false)
	{
		msg.clear();
		size_t pre_len = native ? 0 : _prefix.size() + _suffix.size();
		size_t total_len = packer_helper::msg_size_check(pre_len, pstr, len, num);
		if ((size_t) -1 == total_len)
			return false;
		else if (total_len > pre_len)
		{
			msg.reserve(total_len);
			if (!native)
				msg.append(_prefix);
			for (size_t i = 0; i < num; ++i)
				if (NULL != pstr[i])
					msg.append(pstr[i], len[i]);
			if (!native)
				msg.append(_suffix);
		} //if (total_len > pre_len)

		return true;
	}

	virtual char* raw_data(msg_type& msg) const {return const_cast<char*>(boost::next(msg.data(), _prefix.size()));}
	virtual const char* raw_data(msg_ctype& msg) const {return boost::next(msg.data(), _prefix.size());}
	virtual size_t raw_data_len(msg_ctype& msg) const {return msg.size() - _prefix.size() - _suffix.size();}

private:
	std::string _prefix, _suffix;
};

}} //namespace

#endif /* ST_ASIO_WRAPPER_PACKER_H_ */
