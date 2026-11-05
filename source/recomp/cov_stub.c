/* Stand-ins for the recompiled-code runtime, so the coverage tool can be built before any C has been generated.
 * The coverage run uses the plain interpreter and never calls these. */
#include "rc_runtime.h"
void rc_init(void) {}
void rc_reset_cpu(int which) { (void)which; }
void rc_execute(int which, int cycles) { (void)which; (void)cycles; }
