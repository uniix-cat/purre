#ifndef PURR_SYNTAX_H
#define PURR_SYNTAX_H

#include "buffer.h"
#include "terminal.h"

enum HighlightType {
    HL_NORMAL = 0,
    HL_KEYWORD,
    HL_TYPE,
    HL_STRING,
    HL_COMMENT,
    HL_NUMBER,
    HL_FUNCTION,
    HL_PREPROC
};

void syntax_highlight_line(const Buffer *b, int row_idx, AppendBuffer *ab, int col_offset, int max_cols, int sel_start, int sel_end);

#endif /* PURR_SYNTAX_H */
