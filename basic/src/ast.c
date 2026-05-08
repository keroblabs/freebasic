/*
 * ast.c — AST node constructors + free
 */
#include "ast.h"
#include "system_api.h"
#include <stdlib.h>
#include <string.h>

static ASTNode* ast_alloc(ASTKind kind, int line) {
    ASTNode* node = fb_calloc(1, sizeof(ASTNode));
    node->kind = kind;
    node->line = line;
    return node;
}

ASTNode* ast_literal(int line, FBValue value) {
    ASTNode* n = ast_alloc(AST_LITERAL, line);
    n->data.literal.value = fbval_copy(&value);
    return n;
}

ASTNode* ast_variable(int line, const char* name, FBType type_hint) {
    ASTNode* n = ast_alloc(AST_VARIABLE, line);
    strncpy(n->data.variable.name, name, sizeof(n->data.variable.name) - 1);
    n->data.variable.type_hint = type_hint;
    return n;
}

ASTNode* ast_binop(int line, TokenKind op, ASTNode* left, ASTNode* right) {
    ASTNode* n = ast_alloc(AST_BINARY_OP, line);
    n->data.binop.op = op;
    n->data.binop.left = left;
    n->data.binop.right = right;
    return n;
}

ASTNode* ast_unop(int line, TokenKind op, ASTNode* operand) {
    ASTNode* n = ast_alloc(AST_UNARY_OP, line);
    n->data.unop.op = op;
    n->data.unop.operand = operand;
    return n;
}

ASTNode* ast_func_call(int line, const char* name, ASTNode** args, int arg_count) {
    ASTNode* n = ast_alloc(AST_FUNC_CALL, line);
    strncpy(n->data.func_call.name, name, sizeof(n->data.func_call.name) - 1);
    n->data.func_call.args = args;
    n->data.func_call.arg_count = arg_count;
    return n;
}

ASTNode* ast_paren(int line, ASTNode* expr) {
    ASTNode* n = ast_alloc(AST_PAREN, line);
    n->data.paren.expr = expr;
    return n;
}

ASTNode* ast_print(int line, ASTNode** items, int* seps, int count, int trailing) {
    ASTNode* n = ast_alloc(AST_PRINT, line);
    n->data.print.items = items;
    n->data.print.separators = seps;
    n->data.print.item_count = count;
    n->data.print.trailing_sep = trailing;
    n->data.print.filenum = 0;
    return n;
}

ASTNode* ast_let(int line, ASTNode* target, ASTNode* expr) {
    ASTNode* n = ast_alloc(AST_LET, line);
    n->data.let.target = target;
    n->data.let.expr = expr;
    return n;
}

ASTNode* ast_goto(int line, const char* label, int lineno) {
    ASTNode* n = ast_alloc(AST_GOTO, line);
    if (label)
        strncpy(n->data.jump.label, label, sizeof(n->data.jump.label) - 1);
    n->data.jump.lineno = lineno;
    return n;
}

ASTNode* ast_gosub(int line, const char* label, int lineno) {
    ASTNode* n = ast_alloc(AST_GOSUB, line);
    if (label)
        strncpy(n->data.jump.label, label, sizeof(n->data.jump.label) - 1);
    n->data.jump.lineno = lineno;
    return n;
}

ASTNode* ast_for(int line, ASTNode* var, ASTNode* start, ASTNode* end,
                 ASTNode* step, ASTNode** body, int body_count) {
    ASTNode* n = ast_alloc(AST_FOR, line);
    n->data.for_loop.var = var;
    n->data.for_loop.start = start;
    n->data.for_loop.end = end;
    n->data.for_loop.step = step;
    n->data.for_loop.body = body;
    n->data.for_loop.body_count = body_count;
    return n;
}

ASTNode* ast_end(int line) {
    return ast_alloc(AST_END, line);
}

ASTNode* ast_stop(int line) {
    return ast_alloc(AST_STOP, line);
}

ASTNode* ast_return(int line) {
    return ast_alloc(AST_RETURN, line);
}

ASTNode* ast_rem(int line) {
    return ast_alloc(AST_REM, line);
}

ASTNode* ast_label_def(int line, const char* name) {
    ASTNode* n = ast_alloc(AST_LABEL_DEF, line);
    strncpy(n->data.jump.label, name, sizeof(n->data.jump.label) - 1);
    n->data.jump.lineno = -1;
    return n;
}

ASTNode* ast_exit(int line, int exit_what) {
    ASTNode* n = ast_alloc(AST_EXIT, line);
    n->data.exit_stmt.exit_what = exit_what;
    return n;
}

ASTNode* ast_cls(int line) {
    return ast_alloc(AST_CLS, line);
}

ASTNode* ast_beep(int line) {
    return ast_alloc(AST_BEEP, line);
}

/* Free an AST node and all children */
void ast_free(ASTNode* node) {
    if (!node) return;

    switch (node->kind) {
        case AST_LITERAL:
            fbval_release(&node->data.literal.value);
            break;
        case AST_BINARY_OP:
            ast_free(node->data.binop.left);
            ast_free(node->data.binop.right);
            break;
        case AST_UNARY_OP:
            ast_free(node->data.unop.operand);
            break;
        case AST_FUNC_CALL:
            for (int i = 0; i < node->data.func_call.arg_count; i++)
                ast_free(node->data.func_call.args[i]);
            fb_free(node->data.func_call.args);
            break;
        case AST_PAREN:
            ast_free(node->data.paren.expr);
            break;
        case AST_ARRAY_ACCESS:
            for (int i = 0; i < node->data.array_access.nsubs; i++)
                ast_free(node->data.array_access.subscripts[i]);
            fb_free(node->data.array_access.subscripts);
            break;
        case AST_UDT_MEMBER:
            ast_free(node->data.udt_member.base);
            break;
        case AST_PRINT:
            for (int i = 0; i < node->data.print.item_count; i++)
                ast_free(node->data.print.items[i]);
            fb_free(node->data.print.items);
            fb_free(node->data.print.separators);
            break;
        case AST_LET:
            ast_free(node->data.let.target);
            ast_free(node->data.let.expr);
            break;
        case AST_IF:
            ast_free(node->data.if_block.condition);
            for (int i = 0; i < node->data.if_block.then_count; i++)
                ast_free(node->data.if_block.then_body[i]);
            fb_free(node->data.if_block.then_body);
            for (int i = 0; i < node->data.if_block.elseif_n; i++) {
                ast_free(node->data.if_block.elseif_cond[i]);
                for (int j = 0; j < node->data.if_block.elseif_count[i]; j++)
                    ast_free(node->data.if_block.elseif_body[i][j]);
                fb_free(node->data.if_block.elseif_body[i]);
            }
            fb_free(node->data.if_block.elseif_cond);
            fb_free(node->data.if_block.elseif_body);
            fb_free(node->data.if_block.elseif_count);
            for (int i = 0; i < node->data.if_block.else_count; i++)
                ast_free(node->data.if_block.else_body[i]);
            fb_free(node->data.if_block.else_body);
            break;
        case AST_FOR:
            ast_free(node->data.for_loop.var);
            ast_free(node->data.for_loop.start);
            ast_free(node->data.for_loop.end);
            ast_free(node->data.for_loop.step);
            for (int i = 0; i < node->data.for_loop.body_count; i++)
                ast_free(node->data.for_loop.body[i]);
            fb_free(node->data.for_loop.body);
            break;
        case AST_WHILE:
        case AST_DO_LOOP:
            ast_free(node->data.loop.condition);
            for (int i = 0; i < node->data.loop.body_count; i++)
                ast_free(node->data.loop.body[i]);
            fb_free(node->data.loop.body);
            break;
        case AST_DIM:
        case AST_REDIM:
            for (int i = 0; i < node->data.dim.ndims * 2; i++)
                if (node->data.dim.bounds) ast_free(node->data.dim.bounds[i]);
            fb_free(node->data.dim.bounds);
            break;
        case AST_CONST_DECL:
            ast_free(node->data.const_decl.value_expr);
            break;
        case AST_INPUT:
        case AST_LINE_INPUT:
            ast_free(node->data.input.prompt);
            for (int i = 0; i < node->data.input.var_count; i++)
                ast_free(node->data.input.vars[i]);
            fb_free(node->data.input.vars);
            break;
        case AST_SELECT_CASE:
            ast_free(node->data.select_case.test_expr);
            for (int i = 0; i < node->data.select_case.case_count; i++) {
                for (int j = 0; j < node->data.select_case.cases[i].value_count; j++) {
                    ast_free(node->data.select_case.cases[i].values[j]);
                    ast_free(node->data.select_case.cases[i].range_ends[j]);
                }
                fb_free(node->data.select_case.cases[i].values);
                fb_free(node->data.select_case.cases[i].kinds);
                fb_free(node->data.select_case.cases[i].relops);
                fb_free(node->data.select_case.cases[i].range_ends);
                for (int j = 0; j < node->data.select_case.cases[i].body_count; j++)
                    ast_free(node->data.select_case.cases[i].body[j]);
                fb_free(node->data.select_case.cases[i].body);
            }
            fb_free(node->data.select_case.cases);
            for (int i = 0; i < node->data.select_case.else_count; i++)
                ast_free(node->data.select_case.else_body[i]);
            fb_free(node->data.select_case.else_body);
            break;
        case AST_WRITE_STMT:
            for (int i = 0; i < node->data.write_stmt.item_count; i++)
                ast_free(node->data.write_stmt.items[i]);
            fb_free(node->data.write_stmt.items);
            break;
        case AST_LOCATE:
            ast_free(node->data.locate.row);
            ast_free(node->data.locate.col);
            break;
        case AST_COLOR:
            ast_free(node->data.color.fg);
            ast_free(node->data.color.bg);
            break;
        case AST_PRINT_USING:
            ast_free(node->data.print_using.format_expr);
            for (int i = 0; i < node->data.print_using.item_count; i++)
                ast_free(node->data.print_using.items[i]);
            fb_free(node->data.print_using.items);
            break;
        case AST_DATA:
            for (int i = 0; i < node->data.data.value_count; i++)
                fbval_release(&node->data.data.values[i]);
            fb_free(node->data.data.values);
            break;
        case AST_READ:
            for (int i = 0; i < node->data.read_stmt.var_count; i++)
                ast_free(node->data.read_stmt.vars[i]);
            fb_free(node->data.read_stmt.vars);
            break;
        case AST_SWAP:
            ast_free(node->data.swap.a);
            ast_free(node->data.swap.b);
            break;
        case AST_ON_GOTO:
        case AST_ON_GOSUB:
            ast_free(node->data.on_branch.expr);
            fb_free(node->data.on_branch.labels);
            fb_free(node->data.on_branch.linenos);
            break;
        case AST_SUB_DEF:
        case AST_FUNCTION_DEF:
            fb_free(node->data.proc_def.params);
            for (int i = 0; i < node->data.proc_def.body_count; i++)
                ast_free(node->data.proc_def.body[i]);
            fb_free(node->data.proc_def.body);
            break;
        case AST_CALL:
            for (int i = 0; i < node->data.call.arg_count; i++)
                ast_free(node->data.call.args[i]);
            fb_free(node->data.call.args);
            break;
        case AST_DECLARE:
            fb_free(node->data.declare.params);
            break;
        case AST_DEF_FN:
            fb_free(node->data.def_fn.params);
            ast_free(node->data.def_fn.body_expr);
            for (int i = 0; i < node->data.def_fn.body_count; i++)
                ast_free(node->data.def_fn.body[i]);
            fb_free(node->data.def_fn.body);
            break;
        case AST_OPEN:
            ast_free(node->data.open.filename);
            ast_free(node->data.open.filenum);
            ast_free(node->data.open.reclen);
            break;
        case AST_CLOSE:
            fb_free(node->data.close.filenums);
            break;
        case AST_GET_FILE:
        case AST_PUT_FILE:
            ast_free(node->data.file_io.filenum);
            ast_free(node->data.file_io.recnum);
            ast_free(node->data.file_io.var);
            break;
        case AST_SEEK_STMT:
            ast_free(node->data.seek.filenum);
            ast_free(node->data.seek.position);
            break;
        case AST_LSET:
        case AST_RSET:
            ast_free(node->data.lrset.target);
            ast_free(node->data.lrset.expr);
            break;
        case AST_SHELL:
        case AST_CHDIR:
        case AST_MKDIR:
        case AST_RMDIR:
        case AST_KILL_STMT:
            ast_free(node->data.single_arg.arg);
            break;
        case AST_ERROR_STMT:
            ast_free(node->data.error_stmt.code);
            break;
        case AST_ENVIRON_STMT:
            ast_free(node->data.environ_stmt.expr);
            break;
        case AST_RANDOMIZE:
            ast_free(node->data.randomize.seed);
            break;
        case AST_SLEEP:
            ast_free(node->data.sleep_stmt.seconds);
            break;
        case AST_SOUND:
            ast_free(node->data.sound.freq);
            ast_free(node->data.sound.duration);
            break;
        case AST_VIEW_PRINT:
            ast_free(node->data.view_print.top);
            ast_free(node->data.view_print.bottom);
            break;
        case AST_WIDTH_STMT:
            ast_free(node->data.width_stmt.cols);
            ast_free(node->data.width_stmt.rows);
            break;
        case AST_NAME_STMT:
            ast_free(node->data.name_stmt.old_name);
            ast_free(node->data.name_stmt.new_name);
            break;
        case AST_CHAIN:
            ast_free(node->data.chain.filename);
            ast_free(node->data.chain.start_line);
            break;
        case AST_SHARED_STMT:
        case AST_STATIC_STMT:
            fb_free(node->data.shared_stmt.names);
            fb_free(node->data.shared_stmt.types);
            break;
        case AST_ERASE:
            fb_free(node->data.erase.names);
            break;
        case AST_TYPE_DEF:
            for (int i = 0; i < node->data.type_def.field_count; i++) {
                for (int j = 0; j < node->data.type_def.fields[i].ndims * 2; j++)
                    ast_free(node->data.type_def.fields[i].bounds[j]);
                fb_free(node->data.type_def.fields[i].bounds);
            }
            fb_free(node->data.type_def.fields);
            break;
        case AST_FIELD_STMT:
            ast_free(node->data.field.filenum);
            fb_free(node->data.field.fields);
            break;
        case AST_COMMON:
            fb_free(node->data.common.names);
            fb_free(node->data.common.types);
            fb_free(node->data.common.is_array);
            break;
        case AST_LOCK_STMT:
        case AST_UNLOCK_STMT:
            ast_free(node->data.lock.filenum);
            ast_free(node->data.lock.start_rec);
            ast_free(node->data.lock.end_rec);
            break;
        case AST_POKE:
        case AST_OUT_STMT:
            ast_free(node->data.poke_out.addr);
            ast_free(node->data.poke_out.val);
            break;
        case AST_SCREEN:
            ast_free(node->data.screen.mode);
            ast_free(node->data.screen.colorswitch);
            ast_free(node->data.screen.apage);
            ast_free(node->data.screen.vpage);
            break;
        case AST_PSET:
        case AST_PRESET:
            ast_free(node->data.pset.x);
            ast_free(node->data.pset.y);
            ast_free(node->data.pset.color);
            break;
        case AST_LINE_GFX:
            ast_free(node->data.line_gfx.x1);
            ast_free(node->data.line_gfx.y1);
            ast_free(node->data.line_gfx.x2);
            ast_free(node->data.line_gfx.y2);
            ast_free(node->data.line_gfx.color);
            break;
        case AST_CIRCLE:
            ast_free(node->data.circle.x);
            ast_free(node->data.circle.y);
            ast_free(node->data.circle.radius);
            ast_free(node->data.circle.color);
            ast_free(node->data.circle.start_angle);
            ast_free(node->data.circle.end_angle);
            ast_free(node->data.circle.aspect);
            break;
        case AST_PAINT:
            ast_free(node->data.paint.x);
            ast_free(node->data.paint.y);
            ast_free(node->data.paint.paint_color);
            ast_free(node->data.paint.border_color);
            break;
        case AST_DRAW:
        case AST_PLAY:
            ast_free(node->data.draw_play.cmd_string);
            break;
        case AST_PALETTE:
            ast_free(node->data.palette.attr);
            ast_free(node->data.palette.color);
            break;
        case AST_PCOPY:
            ast_free(node->data.pcopy.src);
            ast_free(node->data.pcopy.dst);
            break;
        case AST_RUN:
            ast_free(node->data.run.arg);
            break;
        case AST_FILES_STMT:
            ast_free(node->data.files.pattern);
            break;
        default:
            break;
    }

    fb_free(node);
}
