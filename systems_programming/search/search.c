#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sorted-list.h"
#include "tokenizer.h"
#include "uthash.h"
#include "util.h"

struct Record {
  int id;
  char *word;
  SortedList *filenames;
  UT_hash_handle hh;
};

int djb2(unsigned char *str)
{
  int c, hash = 5381;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return (hash < 0) ? -hash : hash;
}

void destroyStrings(void *p) {
  char *s = (char *)p;
  free(s);
}

void add_record(struct Record *table, char *word, char *filename)
{
  struct Record *r;

  int record_id = djb2((unsigned char *) word);

  HASH_FIND_INT(table, &record_id, r);
  // linear probing to resolve collisions
  while (r != NULL) {
    // found word
    if (strcmp(r->word, word) == 0) {
      break;
    }
    printf("collisions\n");

    record_id++;
    HASH_FIND_INT(table, &record_id, r);
  }

  if (r == NULL) {
    // found empty spot, proceed to add
    r = (struct Record *)malloc(sizeof(struct Record));
    r->id = record_id;
    r->word = word;
    r->filenames = SLCreate(compareStrings, destroyStrings);
    SLInsert(r->filenames, filename);
    printf("inserting word %s id %d\n", r->word, r->id);

    HASH_ADD_INT(table, id, r);
  } else {
    SLInsert(r->filenames, filename);
  }
}

void parse_file(FILE *fp, struct Record *table)
{
  // parse file into a hashtable of linked lists
  char *linep = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  char *token, *word = NULL, *filename;

  while ((linelen = getline(&linep, &linecap, fp)) != -1) {
    TokenizerT *tokenizer = TKCreate(" \n", linep);
    token = TKGetNextToken(tokenizer);

    if (strcmp(token, "<list>") == 0) {
      // set current word
      token = TKGetNextToken(tokenizer);
      word = (char *)malloc(strlen(token) + 1);
      strcpy(word, token);
    } else if (strcmp(token, "</list>") == 0) {
      // do nothing
    } else {
      // iterate through filenames
      do {
        if (!isnum(token)) {
          filename = (char *)malloc(strlen(token) + 1);
          strcpy(filename, token);
          add_record(table, word, filename);
        }
        free(token);
      } while ((token = TKGetNextToken(tokenizer)));
    }

    free(token);
    TKDestroy(tokenizer);
  }

  if (linep) {
    free(linep);
  }
}

void search_and(void)
{

}

void search_or(void)
{

}

int main(int argc, char **argv)
{
  if (argc != 2) {
    printf("Error: Incorrect format, expecting:\n");
    printf("search <inverted-index file name>\n");
    return 1;
  }

  char *filename = argv[1];
  if (!file_exists(filename)) {
    printf("Error: %s does not exist.\n", filename);
    return 1;
  }

  FILE *fp;
  fp = fopen(filename, "r");

  struct Record *table = NULL;
  parse_file(fp, table);

  struct Record *s, *tmp;
  HASH_ITER(hh, table, s, tmp) {
    printf("word %s id %d\n", s->word, s->id);
  }
  unsigned int num_records;
  num_records = HASH_COUNT(table);
  printf("numrecords %u\n", num_records);

  char *linep = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  // TODO use tokenizer not strtok!
  printf("$ ");
  while ((linelen = getline(&linep, &linecap, stdin)) != -1) {
    TokenizerT *tokenizer = TKCreate(" \n", linep);
    char *cmd = TKGetNextToken(tokenizer);
    if (strcmp(cmd, "q") == 0) {
      // destroy table and free everything
      struct Record *current_record, *tmp;

      HASH_ITER(hh, table, current_record, tmp) {
        HASH_DEL(table, current_record);
        free(current_record->word);
        SLDestroy(current_record->filenames);
        free(current_record);
      }

      free(table);
      free(cmd);
      TKDestroy(tokenizer);
      return 0;
    }

    if (strcmp(cmd, "sa") == 0) {
      search_and();

      printf("$ ");
      continue;
    }

    if (strcmp(cmd, "so") == 0) {
      search_or();

      printf("$ ");
      continue;
    }

    printf("Error: %s: invalid command.\n", cmd);
    printf("$ ");

    free(cmd);
    TKDestroy(tokenizer);
  }

  if(linep) {
    free(linep);
  }

  return 0;
}
