/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: yukeyong                                                     |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_monip.h"

#ifdef NETWARE
#include <netinet/in.h>
#endif

#ifndef PHP_WIN32
#include <netdb.h>
#else
#include "win32/inet.h"
#endif

#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

/* If you declare any globals in php_monip.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(monip)
*/

/* True global resources - no need for thread safety here */
static int le_monip_persist;

/* Whether machine is little endian */
char machine_little_endian;

static int lb_reverse(int a){
	union {
        int i;
        char c[4];
	}u, r;
	
	u.i = a;
	r.c[0] = u.c[3];
	r.c[1] = u.c[2];
	r.c[2] = u.c[1];
	r.c[3] = u.c[0];

	return r.i;
}

/* {{{ monip_functions[]
 *
 * Every user visible function must have an entry in monip_functions[].
 */
const zend_function_entry monip_functions[] = {
	PHP_FE(monip_init, NULL)
	PHP_FE(monip_find, NULL)
	PHP_FE(monip_clear, NULL)
	PHP_FE_END	/* Must be the last line in monip_functions[] */
};
/* }}} */

/* {{{ monip_module_entry
 */
zend_module_entry monip_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"monip",
	monip_functions,
	PHP_MINIT(monip),
	PHP_MSHUTDOWN(monip),
	PHP_RINIT(monip),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(monip),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(monip),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_MONIP_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MONIP
ZEND_GET_MODULE(monip)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("monip.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_monip_globals, monip_globals)
    STD_PHP_INI_ENTRY("monip.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_monip_globals, monip_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_monip_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_monip_init_globals(zend_monip_globals *monip_globals)
{
	monip_globals->global_value = 0;
	monip_globals->global_string = NULL;
}
*/
/* }}} */


/**	 free le_monip_persist
*/
static void monip_data_dtor_persistent(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	php_monip_data *rs = (php_monip_data *)rsrc->ptr;
	//pefree some rc
	pefree(rs->index, 1);
	php_stream_pclose(rs->stream);
	pefree(rs->stream, 1);
	zend_hash_destroy(rs->cache);
	pefree(rs->cache, 1);
	pefree(rs, 1);
}
/* }}} */


/** {{{ cache hashtable dtor function
*/
static void monip_cache_dtor(zval **value){
	if(*value){
		switch(Z_TYPE_PP(value)){
			case IS_STRING:
			case IS_CONSTANT:
				CHECK_ZVAL_STRING(*value);
				pefree(Z_STRVAL_PP(value), 1);
				pefree(*value, 1);
				break;
			case IS_ARRAY:
			case IS_CONSTANT_ARRAY: {
				zend_hash_destroy(Z_ARRVAL_PP(value));
				pefree(Z_ARRVAL_PP(value), 1);
				pefree(*value, 1);
			}
			break;
		}
	}
}
/* }}} */


/** {{{ split str to array
*/
static void monip_split(zval *return_value, char *str, uint str_len TSRMLS_DC){
	char *p1, *p2, *endp;

	array_init(return_value);

	endp = str + str_len;		//end of ip_addr
	p1 = str;
	p2 = zend_memnstr(p1, ZEND_STRL("\t"), endp);

	if(p2 == NULL){
		add_next_index_stringl(return_value, p1, str_len, 1);
	}else{
		do{
			add_next_index_stringl(return_value, p1, p2 - p1, 1);
			p1 = p2 + 1;
		}while((p2 = php_memnstr(p1, ZEND_STRL("\t"), endp)) != NULL);

		if (p1 <= endp){
			add_next_index_stringl(return_value, p1, endp-p1, 1);
		}
	}

}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(monip)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	int machine_endian_check = 1;
	machine_little_endian = ((char *)&machine_endian_check)[0];

	le_monip_persist = zend_register_list_destructors_ex(NULL, monip_data_dtor_persistent, MONIP_DATA_RES_NAME, module_number);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(monip)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(monip)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(monip)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(monip)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "monip support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/** {{{ void public monip_init(string path)
*/
PHP_FUNCTION(monip_init){
	char *fip;
	uint fip_len;
	php_stream *stream;
	int offset;
	php_monip_data *monip;
	zend_rsrc_list_entry le;

	if(zend_hash_exists(&EG(persistent_list), ZEND_STRS(MONIP_HASH_KEY_NAME))){
		RETURN_TRUE;
	}

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &fip, &fip_len) == FAILURE){
		RETURN_FALSE;
	}

	stream = php_stream_open_wrapper(fip, "rb", ENFORCE_SAFE_MODE | STREAM_OPEN_PERSISTENT | REPORT_ERRORS, NULL);
	if(!stream){
		RETURN_FALSE;
	}
	
	php_stream_read(stream, (char *)&offset, 4);
	if(machine_little_endian){
		offset = lb_reverse(offset);
	}

	//php_printf("%d\n", offset);
	monip = pemalloc(sizeof(php_monip_data), 1);
	if(!monip){
		php_stream_pclose(monip->stream);
		RETURN_FALSE;
	}

	monip->offset = offset; 
	monip->index_len = offset - 4;
	monip->index = pemalloc(sizeof(char)*monip->index_len + 1, 1);
	monip->stream = stream;
	//hashtable
	monip->cache = (HashTable *) pemalloc(sizeof(HashTable), 1);
	zend_hash_init(monip->cache, 16, NULL, (dtor_func_t)monip_cache_dtor, 1);

	php_stream_read(stream, monip->index, monip->index_len);

	le.type = le_monip_persist;
	le.ptr = monip;

	if(zend_hash_update(&EG(persistent_list), MONIP_HASH_KEY_NAME, sizeof(MONIP_HASH_KEY_NAME), (void *)&le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE){
		pefree(monip->index, 1);
		php_stream_pclose(monip->stream);
		pefree(monip->stream, 1);
		zend_hash_destroy(monip->cache);
		pefree(monip->cache, 1);
		pefree(monip, 1);

		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} *


/** {{{ void public monip_init(string path)
*/
PHP_FUNCTION(monip_find){
	char *ip_str;
	uint ip_str_len;
	zend_rsrc_list_entry *le;
	php_monip_data *monip;
	ulong lgip;
#ifdef HAVE_INET_PTON
	struct in_addr uip;
#else
	unsigned long int uip;
#endif
	char *ip_str_tok, *ipdot, *ip, *nip;
	uint tmp_offset;
	uint start, max_comp_len, index_offset = 0;
	unsigned char index_length; 
	char *ip_addr;
	zval *ip_cache, **pp_cache;

	if(zend_hash_find(&EG(persistent_list), ZEND_STRS(MONIP_HASH_KEY_NAME), (void **)&le) == FAILURE){
		php_error(E_WARNING, "Haven't call monip_init() method");
		RETURN_STRING("N/A", 1);
	}

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ip_str, &ip_str_len) == FAILURE){
		RETURN_STRING("N/A", 1);
	}

	monip = (php_monip_data *)le->ptr;

	//cache exist
	if(zend_hash_find(monip->cache, ip_str, ip_str_len + 1, (void **)&pp_cache) == SUCCESS){

		monip_split(return_value, Z_STRVAL_PP(pp_cache), Z_STRLEN_PP(pp_cache) TSRMLS_CC);
		return;
	}

#ifdef HAVE_INET_PTON
	if(ip_str_len == 0 || inet_pton(AF_INET, ip_str, &uip) != 1){
		RETURN_STRING("N/A", 1);
	}
	lgip = ntohl(uip.s_addr);
#else
	if (ip_str_len == 0 || (uip = inet_addr(ip_str)) == INADDR_NONE) {
		RETURN_STRING("N/A", 1);
	}
	lgip = ntohl(uip);
#endif

	ip_str_tok = estrndup(ip_str, ip_str_len); 
	ipdot = php_strtok_r(ip_str_tok, ".", &ip);
	tmp_offset = atoi(ipdot) * 4;
	efree(ip_str_tok);

	if(machine_little_endian){
		lgip = lb_reverse(lgip);
	}
	nip = (char *)&lgip;

	start = 0;
	memcpy(&start, monip->index + tmp_offset, 4);
	if(!machine_little_endian){
		start = lb_reverse(start);
	}

	max_comp_len = monip->offset - 1024;

	for(start = start * 8 + 1024; start < max_comp_len; start += 8){

		if(memcmp(monip->index + start, nip, 4) >= 0){
			memcpy(&index_offset, monip->index + start + 4, 3);
			memcpy(&index_offset + 3, "\x0", 1);
			if(!machine_little_endian){
				index_offset = lb_reverse(index_offset);
			}
			index_length = *(monip->index + start + 7);
			break;
		}
	}

	if(index_offset < 1){
		RETURN_STRING("N/A", 1);
	}

	if(php_stream_seek(monip->stream, monip->offset + index_offset - 1024, SEEK_SET) == FAILURE){
		RETURN_STRING("N/A", 1);
	}

	ip_addr = emalloc(sizeof(char) * index_length + 1);			//free ip_addr
	php_stream_read(monip->stream, ip_addr, index_length);
	ip_addr[index_length] = 0;

	monip_split(return_value, ip_addr, index_length TSRMLS_CC);

	ip_cache = (zval *)pemalloc(sizeof(zval), 1);
	INIT_PZVAL(ip_cache);
	Z_STRVAL_P(ip_cache) = pestrndup(ip_addr, index_length, 1);
	Z_STRLEN_P(ip_cache) = index_length;

	efree(ip_addr);

	//add cache
	if(zend_hash_add(monip->cache, ip_str, ip_str_len + 1, &ip_cache, sizeof(zval *), NULL) == FAILURE){
		RETURN_STRING("N/A", 1);
	}

	return;

}
/* }}} *


/** {{{ void public monip_init(string path)
*/
PHP_FUNCTION(monip_clear){
	zend_rsrc_list_entry *le;

	if(zend_hash_find(&EG(persistent_list), ZEND_STRS(MONIP_HASH_KEY_NAME), (void **)&le) == FAILURE){
		RETURN_FALSE;
	}

	zend_hash_del(&EG(persistent_list), MONIP_HASH_KEY_NAME, sizeof(MONIP_HASH_KEY_NAME));
	RETURN_TRUE;
}
/* }}} *


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
