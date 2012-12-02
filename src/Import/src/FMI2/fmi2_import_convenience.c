/*
    Copyright (C) 2012 Modelon AB

    This program is free software: you can redistribute it and/or modify
    it under the terms of the BSD style license.

     This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    FMILIB_License.txt file for more details.

    You should have received a copy of the FMILIB_License.txt file
    along with this program. If not, contact Modelon AB <http://www.modelon.com>.
*/

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <FMI2/fmi2_xml_model_description.h>
#include <FMI2/fmi2_functions.h>

#include "fmi2_import_impl.h"

/**
	\brief Collect model information by counting the number of variables with specific properties and fillinf in fmi2_import_model_counts_t struct.
	\param fmu - An fmu object as returned by fmi2_import_parse_xml().
	\param counts - a pointer to a preallocated struct.
*/
void fmi2_import_collect_model_counts(fmi2_import_t* fmu, fmi2_import_model_counts_t* counts) {
	jm_vector(jm_voidp)* vars = fmi2_xml_get_variables_original_order(fmu->md);
    size_t nv, i;
	if(!vars) return;
    nv = jm_vector_get_size(jm_voidp)(vars);
	memset(counts,0,sizeof(fmi2_import_model_counts_t));
    for(i = 0; i< nv; i++) {
		fmi2_xml_variable_t* var = (fmi2_xml_variable_t*)jm_vector_get_item(jm_voidp)(vars, i); 
		switch (fmi2_xml_get_variability(var)) {
		case fmi2_variability_enu_constant:
			counts->num_constants++;
			break;
		case fmi2_variability_enu_fixed:
			counts->num_fixed++;
			break;
		case fmi2_variability_enu_tunable:
			counts->num_tunable++;
			break;
		case fmi2_variability_enu_discrete:
			counts->num_discrete++;
			break;
		case fmi2_variability_enu_continuous:
			counts->num_continuous++;
			break;
		default:
			assert(0);
		}
		switch(fmi2_xml_get_causality(var)) {
		case fmi2_causality_enu_parameter:
			counts->num_parameters++;
			break;
		case fmi2_causality_enu_input:
			counts->num_inputs++;
			break;
		case fmi2_causality_enu_output:
			counts->num_outputs++;
			break;
		case fmi2_causality_enu_local:
			counts->num_local++;
			break;
		default: assert(0);
		}
		switch(fmi2_xml_get_variable_base_type(var)) {
		case fmi2_base_type_real:
			counts->num_real_vars++;
			break;
		case fmi2_base_type_int:
			counts->num_integer_vars++;
			break;
		case fmi2_base_type_bool:
			counts->num_bool_vars++;
			break;
		case fmi2_base_type_str:
			counts->num_string_vars++;
			break;
		case fmi2_base_type_enum:
			counts->num_enum_vars++;
			break;
		default:
			assert(0);
		}
    }
    return;
}

void fmi2_import_expand_variable_references_impl(fmi2_import_t* fmu, const char* msgIn);

void fmi2_import_expand_variable_references(fmi2_import_t* fmu, const char* msgIn, char* msgOut, size_t maxMsgSize) {
	fmi2_import_expand_variable_references_impl(fmu, msgIn);
	strncpy(msgOut, jm_vector_get_itemp(char)(&fmu->logMessageBuffer,0),maxMsgSize);
}

/* Print msgIn into msgOut by expanding variable references of the form #<Type><VR># into variable names
  and replacing '##' with a single # */
void fmi2_import_expand_variable_references_impl(fmi2_import_t* fmu, const char* msgIn){
	jm_vector(char)* msgOut = &fmu->logMessageBuffer;
	fmi2_xml_model_description_t* md = fmu->md;
	jm_callbacks* callbacks = fmu->callbacks;
    char curCh;
	const char* firstRef;
    size_t i; /* next char index after curCh in msgIn*/ 
	size_t msgLen = strlen(msgIn)+1; /* original message length including terminating 0 */

	if(jm_vector_reserve(char)(msgOut, msgLen + 100) < msgLen + 100) {
		jm_log(fmu->callbacks,"LOGGER", jm_log_level_warning, "Could not allocate memory for the log message");
		jm_vector_resize(char)(msgOut, 6);
		memcpy(jm_vector_get_itemp(char)(msgOut,0),"ERROR",6); /* at least 16 chars are always there */
		return;
	}

	/* check if there are any refs at all and copy the head of the string without references */
	firstRef = strchr(msgIn, '#');
	if(firstRef) {
		i = firstRef - msgIn;
		jm_vector_resize(char)(msgOut, i);
		if(i) {
			memcpy(jm_vector_get_itemp(char)(msgOut, 0), msgIn, i);
		}
		curCh = msgIn[i++];
	}
	else {
		jm_vector_resize(char)(msgOut, msgLen);
		memcpy(jm_vector_get_itemp(char)(msgOut, 0), msgIn, msgLen);
		return;
	}
    do {
        if (curCh!='#') {
            jm_vector_push_back(char)(msgOut, curCh); /* copy in to out */
        }
		else if(msgIn[i] == '#') {
			jm_vector_push_back(char)(msgOut, '#');
			i++; /* skip the second # */
		}
		else {
            unsigned int bufVR;
			fmi2_value_reference_t vr = fmi2_undefined_value_reference;
			char typeChar = msgIn[i++];
			size_t pastePos = jm_vector_get_size(char)(msgOut);
			fmi2_base_type_enu_t baseType;
			size_t num_digits;
			fmi2_xml_variable_t* var;
			const char* name;
			size_t nameLen;
			switch(typeChar) {
				case 'r': 
					baseType = fmi2_base_type_real;
					break;
				case 'i': 
					baseType = fmi2_base_type_int;
					break;
				case 'b': 
					baseType = fmi2_base_type_bool;
					break;
				case 's': 
					baseType = fmi2_base_type_str;
					break;
				default:
					jm_vector_push_back(char)(msgOut, 0);
					jm_log(callbacks,"LOGGER", jm_log_level_warning, 
						"Expected type specification character 'r', 'i', 'b' or 's' in log message here: '%s'", 
					jm_vector_get_itemp(char)(msgOut,0));
                    jm_vector_resize(char)(msgOut, msgLen);
					memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
					return;
			}
            curCh = msgIn[i++];
			while( isdigit(curCh) ) {
				jm_vector_push_back(char)(msgOut, curCh);
	            curCh = msgIn[i++];
			}
			num_digits = jm_vector_get_size(char)(msgOut) - pastePos;
			jm_vector_push_back(char)(msgOut, 0);
			if(num_digits == 0) {
				jm_log(callbacks,"LOGGER", jm_log_level_warning, "Expected value reference in log message here: '%s'", jm_vector_get_itemp(char)(msgOut,0));
                                jm_vector_resize(char)(msgOut, msgLen);
                jm_vector_resize(char)(msgOut, msgLen);
				memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
				return;
			}
			else if(curCh != '#') {
				jm_log(callbacks,"LOGGER", jm_log_level_warning, "Expected terminating '#' in log message here: '%s'", jm_vector_get_itemp(char)(msgOut,0));
                                jm_vector_resize(char)(msgOut, msgLen);
                jm_vector_resize(char)(msgOut, msgLen);
				memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
				return;
			}
			
			if(sscanf(jm_vector_get_itemp(char)(msgOut, pastePos), "%u",&bufVR) != 1) {
				jm_log(callbacks,"LOGGER", jm_log_level_warning, "Could not decode value reference in log message here: '%s'", jm_vector_get_itemp(char)(msgOut,0));
                                jm_vector_resize(char)(msgOut, msgLen);
                jm_vector_resize(char)(msgOut, msgLen);
				memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
				return;
			}
            vr = bufVR;
			var = fmi2_xml_get_variable_by_vr(md,baseType,vr);
			if(!var) {
				jm_log(callbacks,"LOGGER", jm_log_level_warning, "Could not find variable referenced in log message here: '%s'", jm_vector_get_itemp(char)(msgOut,0));
                                jm_vector_resize(char)(msgOut, msgLen);
                jm_vector_resize(char)(msgOut, msgLen);
				memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
				return;
			}
			name = fmi2_xml_get_variable_name(var);
			nameLen = strlen(name);
			if(jm_vector_resize(char)(msgOut, pastePos + nameLen) != pastePos + nameLen) {
				jm_log(callbacks,"LOGGER", jm_log_level_warning, "Could not allocate memory for the log message");
                                jm_vector_resize(char)(msgOut, msgLen);
                jm_vector_resize(char)(msgOut, msgLen);
				memcpy(jm_vector_get_itemp(char)(msgOut,0),msgIn,msgLen);
				return;
			};
			memcpy(jm_vector_get_itemp(char)(msgOut, pastePos), name, nameLen);
        }
        curCh = msgIn[i++];
    } while (curCh);
    jm_vector_push_back(char)(msgOut, 0);
}

void  fmi2_log_forwarding(fmi2_component_environment_t c, fmi2_string_t instanceName, fmi2_status_t status, fmi2_string_t category, fmi2_string_t message, ...) {
    va_list args;
    va_start (args, message);
	fmi2_log_forwarding_v(c, instanceName, status, category, message, args);
    va_end (args);
}

void  fmi2_log_forwarding_v(fmi2_component_environment_t c, fmi2_string_t instanceName, fmi2_status_t status, fmi2_string_t category, fmi2_string_t message, va_list args) {
    char buf[10000], *curp;
	const char* statusStr;
	fmi2_import_t* fmu = (fmi2_import_t*)c;
	jm_callbacks* cb;
	jm_log_level_enu_t logLevel;

	if(fmu) {
		 cb = fmu->callbacks;
	}
	else 
		cb = jm_get_default_callbacks();
	logLevel = cb->log_level;
	switch(status) {
		case fmi2_status_discard:
		case fmi2_status_pending:
		case fmi2_status_ok:
			logLevel = jm_log_level_info;
			break;
		case fmi2_status_warning:
			logLevel = jm_log_level_warning;
			break;
		case fmi2_status_error:
			logLevel = jm_log_level_error;
			break;
		case fmi2_status_fatal:
		default:
			logLevel = jm_log_level_fatal;
	}

    if(logLevel > cb->log_level) return;

	curp = buf;
    *curp = 0;

	if(category) {
        sprintf(curp, "[%s]", category);
        curp += strlen(category)+2;
    }
	statusStr = fmi2_status_to_string(status);
    sprintf(curp, "[FMU status:%s] ", statusStr);
        curp += strlen(statusStr) + strlen("[FMU status:] ");
	vsprintf(curp, message, args);

	if(fmu) {
		fmi2_import_expand_variable_references(fmu, buf, cb->errMessageBuffer,JM_MAX_ERROR_MESSAGE_SIZE);
	}
	else {
		strncpy(cb->errMessageBuffer, buf, JM_MAX_ERROR_MESSAGE_SIZE);
	}
	if(cb->logger) {
		cb->logger(cb, instanceName, logLevel, cb->errMessageBuffer);
	}

}

void  fmi2_default_callback_logger(fmi2_component_environment_t c, fmi2_string_t instanceName, fmi2_status_t status, fmi2_string_t category, fmi2_string_t message, ...) {
    va_list args;
    char buf[500], *curp;
    va_start (args, message);
    curp = buf;
    *curp = 0;
    if(instanceName) {
        sprintf(curp, "[%s]", instanceName);
        curp += strlen(instanceName)+2;
    }
    if(category) {
        sprintf(curp, "[%s]", category);
        curp += strlen(category)+2;
    }
    fprintf(stdout, "%s[status=%s]", buf, fmi2_status_to_string(status));
    vfprintf (stdout, message, args);
    fprintf(stdout, "\n");
    va_end (args);
}

void fmi2_logger(jm_callbacks* cb, jm_string module, jm_log_level_enu_t log_level, jm_string message) {
	fmi2_callback_functions_t* c = (fmi2_callback_functions_t*)cb->context;
	fmi2_status_t status;
	if(!c ||!c->logger) return;

	if(log_level > jm_log_level_all) {
		assert(0);
		status = fmi2_status_error;
	}
	else if(log_level >= jm_log_level_info)
		status = fmi2_status_ok;
	else if(log_level >= jm_log_level_warning)
		status = fmi2_status_warning;
	else if(log_level >= jm_log_level_error)
		status = fmi2_status_error;
	else if(log_level >= jm_log_level_fatal)
		status = fmi2_status_fatal;
	else {
		status = fmi2_status_ok;
	}

	c->logger( c, module, status, jm_log_level_to_string(log_level), message);
}

void fmi2_import_init_logger(jm_callbacks* cb, fmi2_callback_functions_t* fmiCallbacks) {
	cb->logger = fmi2_logger;
	cb->context = fmiCallbacks;
}
