/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "zbxpoller.h"

#include "zbxjson.h"
#include "zbxcacheconfig.h"
#include "zbxsysinfo.h"
#include "zbxcomms.h"
#include "zbxtypes.h"
#include <stddef.h>

void	zbx_agent_prepare_request(struct zbx_json *j, const char *key, int timeout)
{
	char	tmp[MAX_STRING_LEN];

	zbx_json_addstring(j, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_GET_PASSIVE_CHECKS, ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(j, ZBX_PROTO_TAG_DATA);

	zbx_json_addobject(j, NULL);
	zbx_json_addstring(j, ZBX_PROTO_TAG_KEY, key, ZBX_JSON_TYPE_STRING);
	zbx_snprintf(tmp, sizeof(tmp), "%ds", timeout);
	zbx_json_addstring(j, ZBX_PROTO_TAG_TIMEOUT, tmp, ZBX_JSON_TYPE_STRING);
	zbx_json_close(j);
}

int	zbx_agent_handle_response(zbx_socket_t *s, ssize_t received_len, int *ret, char *addr, AGENT_RESULT *result,
		int *version)
{
	zabbix_log(LOG_LEVEL_DEBUG, "get value from agent result: '%s'", s->buffer);

	if (0 == received_len)
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Received empty response from Zabbix Agent at [%s]."
				" Assuming that agent dropped connection because of access permissions.",
				addr));
		*ret = NETWORK_ERROR;
		return SUCCEED;
	}

	if (ZBX_COMPONENT_VERSION(7, 0, 0) <= *version)
	{
		struct zbx_json_parse	jp, jp_data, jp_row;
		const char		*p = NULL;
		size_t			value_alloc = 0;
		char			*value = NULL, tmp[MAX_STRING_LEN];

		if (FAIL == zbx_json_open(s->buffer, &jp))
		{
			*version = 0;
			*ret = FAIL;
			return FAIL;
		}

		if (FAIL == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_VERSION, tmp, sizeof(tmp), NULL))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot find the \"%s\" object in the received JSON"
					" object.", ZBX_PROTO_TAG_VERSION));
			return SUCCEED;
		}

		*version = zbx_get_agent_protocol_version_int(tmp);

		if (SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_ERROR, tmp, sizeof(tmp), NULL))
		{
			zbx_replace_invalid_utf8(tmp);
			SET_MSG_RESULT(result, zbx_strdup(NULL, tmp));
			*ret = NETWORK_ERROR;
			return SUCCEED;
		}

		if (FAIL == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot find the \"%s\" object in the received JSON"
					" object.", ZBX_PROTO_TAG_DATA));
			*ret = NETWORK_ERROR;
			return SUCCEED;
		}

		if (NULL == (p = zbx_json_next(&jp_data, p)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "received empty data response"));
			*ret = NETWORK_ERROR;
			return SUCCEED;
		}

		if (FAIL == zbx_json_brackets_open(p, &jp_row))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot parse response: %s", zbx_json_strerror()));
			*ret = NETWORK_ERROR;
			return SUCCEED;
		}

		if (SUCCEED == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_ERROR, tmp, sizeof(tmp), NULL))
		{
			zbx_replace_invalid_utf8(tmp);
			SET_MSG_RESULT(result, zbx_strdup(NULL, tmp));
			*ret = NOTSUPPORTED;
			return SUCCEED;
		}

		if (FAIL == zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_VALUE, &value, &value_alloc,
				NULL))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot parse response: %s",
					zbx_json_strerror()));
			*ret = NETWORK_ERROR;
			return SUCCEED;
		}

		zbx_set_agent_result_type(result, ITEM_VALUE_TYPE_TEXT, value);
		*ret = SUCCEED;

		zbx_free(value);

		return SUCCEED;
	}

	if (0 == strcmp(s->buffer, ZBX_NOTSUPPORTED))
	{
		/* 'ZBX_NOTSUPPORTED\0<error message>' */
		if (sizeof(ZBX_NOTSUPPORTED) < s->read_bytes)
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "%s", s->buffer + sizeof(ZBX_NOTSUPPORTED)));
		else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Not supported by Zabbix Agent"));

		*ret = NOTSUPPORTED;
	}
	else if (0 == strcmp(s->buffer, ZBX_ERROR))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Zabbix Agent non-critical error"));
		*ret = AGENT_ERROR;
	}
	else
	{
		zbx_set_agent_result_type(result, ITEM_VALUE_TYPE_TEXT, s->buffer);
		*ret = SUCCEED;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieve data from Zabbix agent                                   *
 *                                                                            *
 * Parameters: item             - [IN] item we are interested in              *
 *             config_source_ip - [IN]                                        *
 *             program_type     - [IN]                                        *
 *             result           - [OUT]                                       *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NETWORK_ERROR - network related error occurred               *
 *               NOTSUPPORTED - item not supported by the agent               *
 *               AGENT_ERROR - uncritical error on agent side occurred        *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Comments: error will contain error message                                 *
 *                                                                            *
 ******************************************************************************/
int	zbx_agent_get_value(const zbx_dc_item_t *item, const char *config_source_ip, unsigned char program_type,
		AGENT_RESULT *result, int *version)
{
	zbx_socket_t	s;
	const char	*tls_arg1, *tls_arg2;
	int		ret = SUCCEED, ret_protocol = SUCCEED;
	ssize_t		received_len;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' addr:'%s' key:'%s' conn:'%s'", __func__, item->host.host,
			item->interface.addr, item->key, zbx_tcp_connection_type_name(item->host.tls_connect));

	switch (item->host.tls_connect)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = item->host.tls_issuer;
			tls_arg2 = item->host.tls_subject;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = item->host.tls_psk_identity;
			tls_arg2 = item->host.tls_psk;
			ZBX_UNUSED(program_type);
			break;
#else
		case ZBX_TCP_SEC_TLS_CERT:
		case ZBX_TCP_SEC_TLS_PSK:
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "A TLS connection is configured to be used with agent"
					" but support for TLS was not compiled into %s.",
					get_program_type_string(program_type)));
			ret = CONFIG_ERROR;
			goto out;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid TLS connection parameters."));
			ret = CONFIG_ERROR;
			goto out;
	}

	if (SUCCEED == zbx_tcp_connect(&s, config_source_ip, item->interface.addr, item->interface.port,
			item->timeout + 1, item->host.tls_connect, tls_arg1, tls_arg2))
	{
		struct zbx_json	j;
		char		*ptr;
		size_t		len;

		zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

		zbx_agent_prepare_request(&j, item->key, item->timeout);

		if (ZBX_COMPONENT_VERSION(7, 0, 0) <= *version)
		{
			ptr = j.buffer;
			len = j.buffer_size;
		}
		else
		{
			ptr = item->key;
			len = strlen(item->key);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "Sending [%s]", ptr);

		if (SUCCEED != zbx_tcp_send_ext(&s, ptr, len, 0, ZBX_TCP_PROTOCOL, 0))
		{
			ret = NETWORK_ERROR;
		}
		else if (FAIL != (received_len = zbx_tcp_recv_ext(&s, 0, 0)))
		{
			ret = SUCCEED;
		}
		else if (SUCCEED != zbx_socket_check_deadline(&s))
		{
			ret = TIMEOUT_ERROR;
		}
		else
			ret = NETWORK_ERROR;

		zbx_json_free(&j);
	}
	else
	{
		ret = NETWORK_ERROR;
		goto out;
	}

	if (SUCCEED == ret)
		ret_protocol = zbx_agent_handle_response(&s, received_len, &ret, item->interface.addr, result, version);
	else
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Get value from agent failed: %s", zbx_socket_strerror()));

	zbx_tcp_close(&s);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	/* retry with other protocol */
	if (FAIL == ret_protocol)
		return zbx_agent_get_value(item, config_source_ip, program_type, result, version);

	return ret;
}
