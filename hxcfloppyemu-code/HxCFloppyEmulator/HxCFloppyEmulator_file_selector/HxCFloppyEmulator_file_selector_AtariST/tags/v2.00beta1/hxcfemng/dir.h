#ifndef __DIR_H__
#define __DIR_H__

#include "atari_hw.h"
#include "fat_access.h"

void dir_paginateAndPrefillCurrentPage();
UWORD dir_getNbPages();
UWORD dir_getFirstFileForPage(UWORD page);
UWORD dir_getPageForEntry(UWORD askedEntry);
int dir_getFilesForPage(UWORD page, UWORD *FilelistCurrentPage_tab);
void dir_setFilter(char *filter);
char * dir_getFilter();
int dir_filter(struct fs_dir_ent *dir_entry);
int dir_scan();

#endif
