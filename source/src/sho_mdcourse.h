/* Mega Drive Original Mode courses converted for the arcade engine - see sho_mdcourse.c */
#ifndef SHO_MDCOURSE_H
#define SHO_MDCOURSE_H
typedef struct { int race, md_class, md_stage, stage_id, laps; } mdcourse_t;
int mdcourse_available(void);              /* Mega Drive ROM loaded */
int mdcourse_count(void);                  /* Original Mode races */
int mdcourse_info(int race, mdcourse_t *out);
int mdcourse_install(int race);            /* swap the converted course into the arcade program; 1 = ok */
void mdcourse_uninstall(void);             /* restore the arcade courses */
#endif
