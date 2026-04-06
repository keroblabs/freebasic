#ifndef COERCE_H
#define COERCE_H

#include "value.h"
#include "token.h"

FBType fb_promote_type(FBType a, FBType b);
FBValue fbval_coerce(const FBValue* v, FBType target);
FBValue fbval_binary_op(const FBValue* a, const FBValue* b, TokenKind op);
FBValue fbval_unary_op(const FBValue* v, TokenKind op);
FBValue fbval_compare(const FBValue* a, const FBValue* b, TokenKind op);
FBValue fbval_logical_op(const FBValue* a, const FBValue* b, TokenKind op);

#endif
