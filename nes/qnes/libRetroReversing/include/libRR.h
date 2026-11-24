#ifndef LIBRR_STUB_H
#define LIBRR_STUB_H

/* No-op instrumentation for the RetroReversing QuickNES fork. */
#define libRR_log_instruction(...) ((void)0)
#define libRR_log_instruction_1int(...) ((void)0)
#define libRR_log_instruction_1string(...) ((void)0)

static inline const char *libRR_log_jump_label(...) { return ""; }
static inline const char *libRR_log_long_jump(...) { return ""; }
static inline const char *libRR_log_function_call(...) { return ""; }

#endif
