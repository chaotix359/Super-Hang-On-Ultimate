/* Settings file (.ini) for the macOS frontend; portable C. See ini_file.c. */
#ifndef INI_FILE_H
#define INI_FILE_H
void ini_set_path(const char *path);                       /* the file every call below uses */
int  ini_int(const char *sec, const char *key, int def);   /* the key's number, or def if it is not there */
void ini_str(const char *sec, const char *key, const char *def, char *out, int n);   /* value without "; comment" */
void ini_set(const char *sec, const char *key, const char *val);   /* replaces the key's line, or adds it */
void ini_set_int(const char *sec, const char *key, int v);
#endif
