/*
 * error.c — Error message table + reporting
 */
#include "error.h"
#include <stdio.h>
#include <stdlib.h>

int fb_last_error_code = 0;
int fb_last_error_line = 0;

const char* fb_error_message(FBErrorCode code) {
    switch (code) {
        case FB_ERR_NONE: return "No error";
        case FB_ERR_NEXT_WITHOUT_FOR: return "NEXT without FOR";
        case FB_ERR_SYNTAX: return "Syntax error";
        case FB_ERR_RETURN_WITHOUT_GOSUB: return "RETURN without GOSUB";
        case FB_ERR_OUT_OF_DATA: return "Out of DATA";
        case FB_ERR_ILLEGAL_FUNC_CALL: return "Illegal function call";
        case FB_ERR_OVERFLOW: return "Overflow";
        case FB_ERR_OUT_OF_MEMORY: return "Out of memory";
        case FB_ERR_UNDEFINED_LABEL: return "Label not defined";
        case FB_ERR_SUBSCRIPT_OUT_OF_RANGE: return "Subscript out of range";
        case FB_ERR_DUPLICATE_DEFINITION: return "Duplicate definition";
        case FB_ERR_DIVISION_BY_ZERO: return "Division by zero";
        case FB_ERR_ILLEGAL_IN_DIRECT: return "Illegal in direct mode";
        case FB_ERR_TYPE_MISMATCH: return "Type mismatch";
        case FB_ERR_OUT_OF_STRING_SPACE: return "Out of string space";
        case FB_ERR_STRING_TOO_LONG: return "String too long";
        case FB_ERR_STRING_FORMULA_TOO_COMPLEX: return "String formula too complex";
        case FB_ERR_CANT_CONTINUE: return "Can't continue";
        case FB_ERR_UNDEFINED_FUNCTION: return "Undefined user function";
        case FB_ERR_NO_RESUME: return "No RESUME";
        case FB_ERR_RESUME_WITHOUT_ERROR: return "RESUME without error";
        case FB_ERR_UNPRINTABLE_ERROR: return "Unprintable error";
        case FB_ERR_MISSING_OPERAND: return "Missing operand";
        case FB_ERR_LINE_BUFFER_OVERFLOW: return "Line buffer overflow";
        case FB_ERR_DEVICE_TIMEOUT: return "Device timeout";
        case FB_ERR_DEVICE_FAULT: return "Device fault";
        case FB_ERR_OUT_OF_PAPER: return "Out of paper";
        case FB_ERR_WHILE_WITHOUT_WEND: return "WHILE without WEND";
        case FB_ERR_WEND_WITHOUT_WHILE: return "WEND without WHILE";
        case FB_ERR_DUPLICATE_LABEL: return "Duplicate label";
        case FB_ERR_SUBPROGRAM_NOT_DEFINED: return "Subprogram not defined";
        case FB_ERR_ARGUMENT_COUNT_MISMATCH: return "Argument-count mismatch";
        case FB_ERR_ARRAY_NOT_DEFINED: return "Array not defined";
        case FB_ERR_VARIABLE_REQUIRED: return "Variable required";
        case FB_ERR_FIELD_OVERFLOW: return "FIELD overflow";
        case FB_ERR_INTERNAL_ERROR: return "Internal error";
        case FB_ERR_BAD_FILE_NUMBER: return "Bad file name or number";
        case FB_ERR_FILE_NOT_FOUND: return "File not found";
        case FB_ERR_BAD_FILE_MODE: return "Bad file mode";
        case FB_ERR_FILE_ALREADY_OPEN: return "File already open";
        case FB_ERR_FIELD_STATEMENT_ACTIVE: return "FIELD statement active";
        case FB_ERR_DEVICE_IO_ERROR: return "Device I/O error";
        case FB_ERR_FILE_ALREADY_EXISTS: return "File already exists";
        case FB_ERR_BAD_RECORD_LENGTH: return "Bad record length";
        case FB_ERR_DISK_FULL: return "Disk full";
        case FB_ERR_INPUT_PAST_END: return "Input past end of file";
        case FB_ERR_BAD_RECORD_NUMBER: return "Bad record number";
        case FB_ERR_BAD_FILE_NAME: return "Bad file name";
        case FB_ERR_TOO_MANY_FILES: return "Too many files";
        case FB_ERR_DEVICE_UNAVAILABLE: return "Device unavailable";
        case FB_ERR_COMMUNICATION_BUFFER_OVERFLOW: return "Communication-buffer overflow";
        case FB_ERR_PERMISSION_DENIED: return "Permission denied";
        case FB_ERR_DISK_NOT_READY: return "Disk not ready";
        case FB_ERR_DISK_MEDIA_ERROR: return "Disk-media error";
        case FB_ERR_FEATURE_UNAVAILABLE: return "Feature unavailable";
        case FB_ERR_RENAME_ACROSS_DISKS: return "Rename across disks";
        case FB_ERR_PATH_FILE_ACCESS_ERROR: return "Path/File access error";
        case FB_ERR_PATH_NOT_FOUND: return "Path not found";
    }
    return "Unknown error";
}

/* Error handler function pointer (set by interpreter for ON ERROR) */
static void (*error_handler)(FBErrorCode code, int line, const char* extra) = NULL;

void fb_set_error_handler(void (*handler)(FBErrorCode, int, const char*)) {
    error_handler = handler;
}

void fb_error(FBErrorCode code, int line, const char* extra_msg) {
    fb_last_error_code = code;
    fb_last_error_line = line;

    if (error_handler) {
        error_handler(code, line, extra_msg);
        return;
    }

    fprintf(stderr, "Runtime error %d", code);
    if (line > 0)
        fprintf(stderr, " at line %d", line);
    fprintf(stderr, ": %s", fb_error_message(code));
    if (extra_msg)
        fprintf(stderr, " (%s)", extra_msg);
    fprintf(stderr, "\n");
    exit(1);
}

void fb_syntax_error(int line, int col, const char* msg) {
    fprintf(stderr, "Syntax error at line %d", line);
    if (col > 0)
        fprintf(stderr, ", col %d", col);
    fprintf(stderr, ": %s\n", msg);
    exit(1);
}
