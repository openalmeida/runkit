/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group, (c) 2008-2012 Dmitry Zenovich |
  +----------------------------------------------------------------------+
  | This source file is subject to the new BSD license,                  |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.opensource.org/licenses/BSD-3-Clause                      |
  | If you did not receive a copy of the license and are unable to       |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  | Modified by Dmitry Zenovich <dzenovich@gmail.com>                    |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_runkit.h"

#ifdef PHP_RUNKIT_MANIPULATION

#ifndef ZEND_ENGINE_2
/* {{{ _php_runkit_locate_scope
    ZendEngine 1 hack to determine a function's scope */
zend_class_entry *_php_runkit_locate_scope(zend_class_entry *ce, zend_function *fe, const char *methodname_lower, int methodname_len TSRMLS_DC)
{
	zend_class_entry *current = ce->parent, *top = ce;
	zend_function *func;

	while (current) {
		if (zend_hash_find(&current->function_table, (char *) methodname_lower, methodname_len + 1, (void*)&func) == FAILURE) {
			/* Not defined at this point (or higher) */
			return top;
		}
		if (func->op_array.opcodes != fe->op_array.opcodes) {
			/* Different function above this point */
			return top;
		}
		/* Same opcodes */
		top = current;

		current = current->parent;
	}

	return top;
}
/* }}} */
#else
/* {{{ _php_runkit_get_method_prototype
	Locates the prototype method */
static inline zend_function* _php_runkit_get_method_prototype(zend_class_entry *ce, const char* func_lower, int func_len TSRMLS_DC) {
	zend_class_entry *pce = ce;
	zend_function *proto = NULL;

	while (pce) {
		if (zend_hash_find(&pce->function_table, (char *) func_lower, func_len+1, (void*) &proto) != FAILURE) {
			break;
		}
		pce = pce->parent;
	}
	if (!pce) {
		proto = NULL;
	}
	return proto;
}
/* }}} */
#endif

/* {{{ php_runkit_fetch_class_int
 */
int php_runkit_fetch_class_int(const char *classname, int classname_len, zend_class_entry **pce TSRMLS_DC)
{
	char *lclass;
	zend_class_entry *ce;
#ifdef ZEND_ENGINE_2
	zend_class_entry **ze;
#endif

	/* Ignore leading "\" */
	if (classname[0] == '\\') {
		++classname;
		--classname_len;
	}

	lclass = estrndup(classname, classname_len);
	if (lclass == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		return FAILURE;
	}
	php_strtolower(lclass, classname_len);

#ifdef ZEND_ENGINE_2
	if (zend_hash_find(EG(class_table), lclass, classname_len + 1, (void*)&ze) == FAILURE ||
		!ze || !*ze) {
		efree(lclass);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s not found", classname);
		return FAILURE;
	}
	ce = *ze;
#else
	if (zend_hash_find(EG(class_table), lclass, classname_len + 1, (void*)&ce) == FAILURE ||
		!ce) {
		efree(lclass);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s not found", classname);
		return FAILURE;
	}
#endif

	if (pce) {
		*pce = ce;
	}

	efree(lclass);
	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_fetch_class
 */
int php_runkit_fetch_class(const char *classname, int classname_len, zend_class_entry **pce TSRMLS_DC)
{
	zend_class_entry *ce;

	if (php_runkit_fetch_class_int(classname, classname_len, &ce TSRMLS_CC) == FAILURE) {
		return FAILURE;
	}

	if (ce->type != ZEND_USER_CLASS) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s is not a user-defined class", classname);
		return FAILURE;
	}

#ifdef ZEND_ENGINE_2
	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s is an interface", classname);
		return FAILURE;
	}
#endif

	if (pce) {
		*pce = ce;
	}

	return SUCCESS;
}
/* }}} */

#ifdef ZEND_ENGINE_2
/* {{{ php_runkit_fetch_interface
 */
int php_runkit_fetch_interface(const char *classname, int classname_len, zend_class_entry **pce TSRMLS_DC)
{
	char *lclass;

	/* Ignore leading "\" */
	if (classname[0] == '\\') {
		++classname;
		--classname_len;
	}

	lclass = estrndup(classname, classname_len);
	if (lclass == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		return FAILURE;
	}
	php_strtolower(lclass, classname_len);

	if (zend_hash_find(EG(class_table), lclass, classname_len + 1, (void*)&pce) == FAILURE ||
		!pce || !*pce) {
		efree(lclass);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "interface %s not found", classname);
		return FAILURE;
	}

	if (!((*pce)->ce_flags & ZEND_ACC_INTERFACE)) {
		efree(lclass);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s is not an interface", classname);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */
#endif

/* {{{ php_runkit_fetch_class_method
 */
static int php_runkit_fetch_class_method(const char *classname, int classname_len, const char *fname, int fname_len, zend_class_entry **pce, zend_function **pfe
TSRMLS_DC)
{
	HashTable *ftable = EG(function_table);
	zend_class_entry *ce;
	zend_function *fe;
	char *fname_lower;

	if (php_runkit_fetch_class_int(classname, classname_len, &ce TSRMLS_CC) == FAILURE) {
		return FAILURE;
	}

	if (ce->type != ZEND_USER_CLASS) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "class %s is not a user-defined class", classname);
		return FAILURE;
	}

	if (pce) {
		*pce = ce;
	}

	ftable = &ce->function_table;

	fname_lower = estrndup(fname, fname_len);
	if (fname_lower == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		return FAILURE;
	}
	php_strtolower(fname_lower, fname_len);

	if (zend_hash_find(ftable, fname_lower, fname_len + 1, (void*)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s() not found", classname, fname);
		efree(fname_lower);
		return FAILURE;
	}

	if (fe->type != ZEND_USER_FUNCTION) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s() is not a user function", classname, fname);
		efree(fname_lower);
		return FAILURE;
	}

	if (pfe) {
		*pfe = fe;
	}

	efree(fname_lower);
	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_update_children_methods
	Scan the class_table for children of the class just updated */
int php_runkit_update_children_methods(RUNKIT_53_TSRMLS_ARG(zend_class_entry *ce), int num_args, va_list args, zend_hash_key *hash_key)
{
	zend_class_entry *ancestor_class =  va_arg(args, zend_class_entry*);
	zend_class_entry *parent_class =  va_arg(args, zend_class_entry*);
	zend_class_entry *scope;
	zend_function *fe =  va_arg(args, zend_function*);
	char *fname_lower = va_arg(args, char*);
	int fname_len = va_arg(args, int);
	zend_function *orig_fe = va_arg(args, zend_function*);
#ifndef ZEND_ENGINE_2
	int is_constr = va_arg(args, int);
#endif
	zend_function *cfe = NULL;

	RUNKIT_UNDER53_TSRMLS_FETCH();

#ifdef ZEND_ENGINE_2
	ce = *((zend_class_entry**)ce);
#endif

	if (ce->parent != parent_class) {
		/* Not a child, ignore */
		return ZEND_HASH_APPLY_KEEP;
	}

	if (zend_hash_find(&ce->function_table, fname_lower, fname_len + 1, (void*)&cfe) == SUCCESS) {
		scope = php_runkit_locate_scope(ce, cfe, fname_lower, fname_len);
		if (scope != ancestor_class) {
			/* This method was defined below our current level, leave it be */
#ifdef ZEND_ENGINE_2
			cfe->common.prototype = _php_runkit_get_method_prototype(scope->parent, fname_lower, fname_len TSRMLS_CC);
			zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 6, ancestor_class, ce, fe, fname_lower, fname_len, orig_fe);
#endif
			return ZEND_HASH_APPLY_KEEP;
		}
	}

#ifndef ZEND_ENGINE_2
	/* Process children of this child */
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 7,
	                               ancestor_class, ce, fe, fname_lower, fname_len, orig_fe,
	                               is_constr || (fname_len == ce->name_length && !strncmp(ce->name, fname_lower, ce->name_length))
	);
#endif

	if (cfe) {
#ifdef ZEND_ENGINE_2
		php_runkit_remove_function_from_reflection_objects(cfe TSRMLS_CC);
#endif
		if (zend_hash_del(&ce->function_table, fname_lower, fname_len + 1) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error updating child class");
			return ZEND_HASH_APPLY_KEEP;
		}
	}

	if (zend_hash_add(&ce->function_table, fname_lower, fname_len + 1, fe, sizeof(zend_function), NULL) ==  FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error updating child class");
		return ZEND_HASH_APPLY_KEEP;
	}
	PHP_RUNKIT_FUNCTION_ADD_REF(fe);
	PHP_RUNKIT_INHERIT_MAGIC(ce, fe, orig_fe, is_constr);

#ifdef ZEND_ENGINE_2
	/* Process children of this child */
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 6,
	                               ancestor_class, ce, fe, fname_lower, fname_len, orig_fe);
#endif

	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

/* {{{ php_runkit_clean_children
	Scan the class_table for children of the class just updated */
int php_runkit_clean_children_methods(RUNKIT_53_TSRMLS_ARG(zend_class_entry *ce), int num_args, va_list args, zend_hash_key *hash_key)
{
	zend_class_entry *ancestor_class =  va_arg(args, zend_class_entry*);
	zend_class_entry *parent_class =  va_arg(args, zend_class_entry*);
	zend_class_entry *scope;
	char *fname_lower = va_arg(args, char*);
	int fname_len = va_arg(args, int);
	zend_function *orig_cfe = va_arg(args, zend_function *);
	zend_function *cfe = NULL;
	RUNKIT_UNDER53_TSRMLS_FETCH();

#ifdef ZEND_ENGINE_2
	ce = *((zend_class_entry**)ce);
#endif

	if (ce->parent != parent_class) {
		/* Not a child, ignore */
		return ZEND_HASH_APPLY_KEEP;
	}

	if (zend_hash_find(&ce->function_table, fname_lower, fname_len + 1, (void*)&cfe) == SUCCESS) {
		scope = php_runkit_locate_scope(ce, cfe, fname_lower, fname_len);
		if (scope != ancestor_class) {
			/* This method was defined below our current level, leave it be */
			return ZEND_HASH_APPLY_KEEP;
		}
	}

	if (!cfe) {
		/* Odd.... nothing to destroy.... */
		return ZEND_HASH_APPLY_KEEP;
	}

	/* Process children of this child */
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_clean_children_methods, 5, ancestor_class, ce, fname_lower, fname_len, orig_cfe);

#ifdef ZEND_ENGINE_2
	php_runkit_remove_function_from_reflection_objects(cfe TSRMLS_CC);
#endif

	zend_hash_del(&ce->function_table, fname_lower, fname_len + 1);

	PHP_RUNKIT_DEL_MAGIC_METHOD(ce, orig_cfe);

	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

/* {{{ php_runkit_method_add_or_update
 */
static void php_runkit_method_add_or_update(INTERNAL_FUNCTION_PARAMETERS, int add_or_update)
{
	const char *classname, *methodname, *arguments, *phpcode;
	int classname_len, methodname_len, arguments_len, phpcode_len;
	zend_class_entry *ce, *ancestor_class = NULL;
	zend_function func, *fe, *orig_fe = NULL;
	char *methodname_lower;
	long argc = ZEND_NUM_ARGS();
#ifdef ZEND_ENGINE_2
	long flags = ZEND_ACC_PUBLIC;
#else
	long flags = 0;
#endif

	if (zend_parse_parameters(argc TSRMLS_CC, "s/s/ss|l",
	                          &classname, &classname_len,
	                          &methodname, &methodname_len,
	                          &arguments, &arguments_len,
	                          &phpcode, &phpcode_len,
	                          &flags) == FAILURE) {
		RETURN_FALSE;
	}

	if (!classname_len || !methodname_len) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Empty parameter given");
		RETURN_FALSE;
	}

	methodname_lower = estrndup(methodname, methodname_len);
	if (methodname_lower == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		RETURN_FALSE;
	}
	php_strtolower(methodname_lower, methodname_len);

	if (add_or_update == HASH_UPDATE) {
		if (php_runkit_fetch_class_method(classname, classname_len, methodname, methodname_len, &ce, &fe TSRMLS_CC) == FAILURE) {
			efree(methodname_lower);
			RETURN_FALSE;
		}
		ancestor_class = php_runkit_locate_scope(ce, fe, methodname_lower, methodname_len);
		orig_fe = fe;

		if (php_runkit_check_call_stack(&fe->op_array TSRMLS_CC) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Cannot redefine a method while that method is active.");
			efree(methodname_lower);
			RETURN_FALSE;
		}
	} else {
		if (php_runkit_fetch_class(classname, classname_len, &ce TSRMLS_CC) == FAILURE) {
			efree(methodname_lower);
			RETURN_FALSE;
		}
		ancestor_class = ce;
	}

	if (php_runkit_generate_lambda_method(arguments, arguments_len, phpcode, phpcode_len, &fe,
	                                      (flags & PHP_RUNKIT_ACC_RETURN_REFERENCE) == PHP_RUNKIT_ACC_RETURN_REFERENCE
	                                      TSRMLS_CC) == FAILURE) {
		efree(methodname_lower);
		RETURN_FALSE;
	}

	func = *fe;
	PHP_RUNKIT_FUNCTION_ADD_REF(&func);
	efree((void*)func.common.function_name);
	func.common.function_name = estrndup(methodname, methodname_len);
#ifdef ZEND_ENGINE_2
	if (flags & ZEND_ACC_PRIVATE) {
		func.common.fn_flags &= ~ZEND_ACC_PPP_MASK;
		func.common.fn_flags |= ZEND_ACC_PRIVATE;
	} else if (flags & ZEND_ACC_PROTECTED) {
		func.common.fn_flags &= ~ZEND_ACC_PPP_MASK;
		func.common.fn_flags |= ZEND_ACC_PROTECTED;
	} else {
		func.common.fn_flags &= ~ZEND_ACC_PPP_MASK;
		func.common.fn_flags |= ZEND_ACC_PUBLIC;
	}

	if (flags & ZEND_ACC_STATIC) {
		func.common.fn_flags |= ZEND_ACC_STATIC;
	} else {
		func.common.fn_flags |= ZEND_ACC_ALLOW_STATIC;
	}
#endif

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 4) || (PHP_MAJOR_VERSION > 5)
	php_runkit_clear_all_functions_runtime_cache(TSRMLS_C);
#endif

#if PHP_MAJOR_VERSION < 5
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 7,
	                               ancestor_class, ce, &func, methodname_lower, methodname_len, orig_fe,
	                               methodname_len == ce->name_length && !strncmp(ce->name, methodname_lower, ce->name_length));
#endif

#ifdef ZEND_ENGINE_2
	if(orig_fe) {
		php_runkit_remove_function_from_reflection_objects(orig_fe TSRMLS_CC);
	}
#endif

	if (zend_hash_add_or_update(&ce->function_table, methodname_lower, methodname_len + 1, &func, sizeof(zend_function), NULL, add_or_update) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add method to class");
		efree(methodname_lower);
		RETURN_FALSE;
	}

	if (zend_hash_del(EG(function_table), RUNKIT_TEMP_FUNCNAME, sizeof(RUNKIT_TEMP_FUNCNAME)) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove temporary function entry");
		efree(methodname_lower);
		RETURN_FALSE;
	}

	if (zend_hash_find(&ce->function_table, methodname_lower, methodname_len + 1, (void*)&fe) == FAILURE ||
		!fe) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to locate newly added method");
		efree(methodname_lower);
		RETURN_FALSE;
	}

#ifdef ZEND_ENGINE_2
	fe->common.scope = ce;
	fe->common.prototype = _php_runkit_get_method_prototype(ce->parent, methodname_lower, methodname_len TSRMLS_CC);
#endif

	PHP_RUNKIT_ADD_MAGIC_METHOD(ce, methodname_lower, methodname_len, fe, orig_fe);
#if PHP_MAJOR_VERSION >= 5
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 6,
	                               ancestor_class, ce, fe, methodname_lower, methodname_len, orig_fe);
#endif

	efree(methodname_lower);

	RETURN_TRUE;
}
/* }}} */

/* {{{ php_runkit_method_copy
 */
static int php_runkit_method_copy(const char *dclass, int dclass_len, const char *dfunc, int dfunc_len,
                                  const char *sclass, int sclass_len, const char *sfunc, int sfunc_len TSRMLS_DC)
{
	zend_class_entry *dce, *sce;
	zend_function dfe, *sfe, *dfeInHashTable;
	char *dfunc_lower;

	if (php_runkit_fetch_class_method(sclass, sclass_len, sfunc, sfunc_len, &sce, &sfe TSRMLS_CC) == FAILURE) {
		return FAILURE;
	}

	if (php_runkit_fetch_class(dclass, dclass_len, &dce TSRMLS_CC) == FAILURE) {
		return FAILURE;
	}

	dfunc_lower = estrndup(dfunc, dfunc_len);
	if (dfunc_lower == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		return FAILURE;
	}
	php_strtolower(dfunc_lower, dfunc_len);

	if (zend_hash_exists(&dce->function_table, dfunc_lower, dfunc_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Destination method %s::%s() already exists", dclass, dfunc);
		efree(dfunc_lower);
		return FAILURE;
	}

	dfe = *sfe;
	php_runkit_function_copy_ctor(&dfe, dfunc, dfunc_len TSRMLS_CC);

	if (zend_hash_add(&dce->function_table, dfunc_lower, dfunc_len + 1, &dfe, sizeof(zend_function), (void*) &dfeInHashTable) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error adding method to class %s::%s()", dclass, dfunc);
		efree(dfunc_lower);
		return FAILURE;
	}

#ifdef ZEND_ENGINE_2
	dfeInHashTable->common.scope = dce;
	dfeInHashTable->common.prototype = _php_runkit_get_method_prototype(dce->parent, dfunc_lower, dfunc_len TSRMLS_CC);
#endif

	PHP_RUNKIT_ADD_MAGIC_METHOD(dce, dfunc_lower, dfunc_len, dfeInHashTable, NULL);

	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 7,
	                               dce, dce, dfeInHashTable, dfunc_lower, dfunc_len, NULL,
#ifdef ZEND_ENGINE_2
	                               0
#else
	                               dfunc_len == dce->name_length && !strncmp(dce->name, dfunc_lower, dce->name_length)
#endif
	);

	efree(dfunc_lower);
	return SUCCESS;
}
/* }}} */

/* **************
   * Method API *
   ************** */

/* {{{ proto bool runkit_method_add(string classname, string methodname, string args, string code[, long flags])
	Add a method to an already defined class */
PHP_FUNCTION(runkit_method_add)
{
	php_runkit_method_add_or_update(INTERNAL_FUNCTION_PARAM_PASSTHRU, HASH_ADD);
}
/* }}} */

/* {{{ proto bool runkit_method_redefine(string classname, string methodname, string args, string code[, long flags])
	Redefine an already defined class method */
PHP_FUNCTION(runkit_method_redefine)
{
	php_runkit_method_add_or_update(INTERNAL_FUNCTION_PARAM_PASSTHRU, HASH_UPDATE);
}
/* }}} */

/* {{{ proto bool runkit_method_remove(string classname, string methodname)
	Remove a method from a class definition */
PHP_FUNCTION(runkit_method_remove)
{
	char *classname, *methodname;
	int classname_len, methodname_len;
	zend_class_entry *ce, *ancestor_class = NULL;
	zend_function *fe;
	char *methodname_lower;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/", &classname, &classname_len,
	                                                             &methodname, &methodname_len) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't parse parameters");
		RETURN_FALSE;
	}

	if (!classname_len || !methodname_len) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Empty parameter given");
		RETURN_FALSE;
	}

	if (php_runkit_fetch_class_method(classname, classname_len, methodname, methodname_len, &ce, &fe TSRMLS_CC) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown method %s::%s()", classname, methodname);
		RETURN_FALSE;
	}

	methodname_lower = estrndup(methodname, methodname_len);
	if (methodname_lower == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		RETURN_FALSE;
	}
	php_strtolower(methodname_lower, methodname_len);

	ancestor_class = php_runkit_locate_scope(ce, fe, methodname_lower, methodname_len);

	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_clean_children_methods, 5, ancestor_class, ce, methodname_lower, methodname_len, fe);

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 4) || (PHP_MAJOR_VERSION > 5)
	php_runkit_clear_all_functions_runtime_cache(TSRMLS_C);
#endif

#ifdef ZEND_ENGINE_2
	php_runkit_remove_function_from_reflection_objects(fe TSRMLS_CC);
#endif

	if (zend_hash_del(&ce->function_table, methodname_lower, methodname_len + 1) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove method from class");
		efree(methodname_lower);
		RETURN_FALSE;
	}

	efree(methodname_lower);
	PHP_RUNKIT_DEL_MAGIC_METHOD(ce, fe);

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool runkit_method_rename(string classname, string methodname, string newname)
	Rename a method within a class */
PHP_FUNCTION(runkit_method_rename)
{
	const char *classname, *methodname, *newname;
	int classname_len, methodname_len, newname_len;
	zend_class_entry *ce, *ancestor_class = NULL;
	zend_function *fe, func;
	char *newname_lower, *methodname_lower;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/s/",	&classname, &classname_len,
	                          &methodname, &methodname_len, &newname, &newname_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (!classname_len || !methodname_len || !newname_len) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Empty parameter given");
		RETURN_FALSE;
	}

	if (php_runkit_fetch_class_method(classname, classname_len, methodname, methodname_len, &ce, &fe TSRMLS_CC) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown method %s::%s()", classname, methodname);
		RETURN_FALSE;
	}

	newname_lower = estrndup(newname, newname_len);
	if (newname_lower == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		RETURN_FALSE;
	}
	methodname_lower = estrndup(methodname, methodname_len);
	if (methodname_lower == NULL) {
		efree(newname_lower);
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not enough memory");
		RETURN_FALSE;
	}
	php_strtolower(newname_lower, newname_len);
	php_strtolower(methodname_lower, methodname_len);

	if (zend_hash_exists(&ce->function_table, newname_lower, newname_len + 1)) {
		efree(newname_lower);
		efree(methodname_lower);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s() already exists", classname, newname);
		RETURN_FALSE;
	}

	ancestor_class = php_runkit_locate_scope(ce, fe, methodname_lower, methodname_len);
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_clean_children_methods, 5,
	                               ancestor_class, ce, methodname_lower, methodname_len, fe);

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 4) || (PHP_MAJOR_VERSION > 5)
	php_runkit_clear_all_functions_runtime_cache(TSRMLS_C);
#endif

	func = *fe;
	PHP_RUNKIT_FUNCTION_ADD_REF(&func);
	efree((void*) func.common.function_name);
	func.common.function_name = estrndup(newname, newname_len + 1);

	if (zend_hash_add(&ce->function_table, newname_lower, newname_len + 1, &func, sizeof(zend_function), NULL) == FAILURE) {
		efree(newname_lower);
		efree(methodname_lower);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add new reference to class method");
		zend_function_dtor(&func);
		RETURN_FALSE;
	}

#ifdef ZEND_ENGINE_2
	php_runkit_remove_function_from_reflection_objects(fe TSRMLS_CC);
#endif

	if (zend_hash_del(&ce->function_table, methodname_lower, methodname_len + 1) == FAILURE) {
		efree(newname_lower);
		efree(methodname_lower);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove old method reference from class");
		RETURN_FALSE;
	}

	PHP_RUNKIT_DEL_MAGIC_METHOD(ce, fe);

	if (php_runkit_fetch_class_method(classname, classname_len, newname, newname_len, &ce, &fe TSRMLS_CC) == FAILURE) {
		efree(newname_lower);
		efree(methodname_lower);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to locate newly renamed method");
		RETURN_FALSE;
	}

	PHP_RUNKIT_ADD_MAGIC_METHOD(ce, newname_lower, newname_len, fe, NULL);
	zend_hash_apply_with_arguments(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), (apply_func_args_t)php_runkit_update_children_methods, 7,
	                               ce, ce, fe, newname_lower, newname_len, NULL,
#ifdef ZEND_ENGINE_2
	                               0
#else
	                               newname_len == ce->name_length && !strncmp(ce->name, newname_lower, ce->name_length)
#endif
	);

	efree(newname_lower);
	efree(methodname_lower);

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool runkit_method_copy(string destclass, string destmethod, string srcclass[, string srcmethod])
	Copy a method from one name to another or from one class to another */
PHP_FUNCTION(runkit_method_copy)
{
	const char *dclass, *dfunc, *sclass, *sfunc = NULL;
	int dclass_len, dfunc_len, sclass_len, sfunc_len = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/s/|s/", &dclass,    &dclass_len,
	                                                                  &dfunc,     &dfunc_len,
	                                                                  &sclass,    &sclass_len,
	                                                                  &sfunc,     &sfunc_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (!sfunc) {
		sfunc = dfunc;
		sfunc_len = dfunc_len;
	}

	RETURN_BOOL(php_runkit_method_copy( dclass, dclass_len, dfunc, dfunc_len,
	                                    sclass, sclass_len, sfunc, sfunc_len TSRMLS_CC) == SUCCESS ? 1 : 0);
}
/* }}} */
#endif /* PHP_RUNKIT_MANIPULATION */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

