#include "prg2wav_utils.h"
#include "t64utils.h"
#include "block_list.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char* get_basename(const char *filename)
{
#ifdef _MSC_VER
  char fname[_MAX_FNAME];
  char ext[_MAX_EXT];
  char *dest;

  _splitpath(filename, NULL, NULL, fname, ext);
  dest = (char *)malloc(strlen(fname) + strlen(ext) + 1);
  strcpy(dest, fname);
  strcat(dest,ext);
  return dest;
#else
  return strdup(basename(filename));
#endif
}

struct simple_block_list_element** add_all_entries_from_file(struct simple_block_list_element **block, FILE *fd)
{
  struct simple_block_list_element **current_block = block;
  int i;

  for (i = 1; i <= get_total_entries(fd); i++)
  {
    add_simple_block_list_element(current_block);
    if (!get_entry(i, fd, &(*current_block)->block)){
      remove_simple_block_list_element(current_block);
      continue;
    }
    current_block = &(*current_block)->next;
  }

  return current_block;
}

void put_filename_in_entryname(const char *filename, char *entryname){
  int i;
  int maxchar;
  /* first, strip off path from filename */
  char *stripped = get_basename(filename);

  /* then ignore .prg at end if present */
  maxchar = strlen(stripped);
  if (maxchar >= 4 &&
      (strcmp(stripped + maxchar - 4, ".prg") == 0 ||
       strcmp(stripped + maxchar - 4, ".PRG") == 0))
    maxchar -= 4;

  /* copy everything left */
  for (i = 0; i < 16; i++)
    if (i < maxchar)
      entryname[i] = toupper(stripped[i]);
    else
      entryname[i] = ' ';
  entryname[16] = 0;
  free(stripped);
}