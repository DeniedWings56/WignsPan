#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>

#include "kvs.h"
#include "constants.h"

static struct HashTable *kvs_table = NULL;

pthread_rwlock_t kvs_lock = PTHREAD_RWLOCK_INITIALIZER;

static struct timespec delay_to_timespec(unsigned int delay_ms)
{
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

int kvs_init()
{
  if (kvs_table != NULL)
  {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate()
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  free_table(kvs_table);
  return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE])
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_wrlock(&kvs_lock);

  for (size_t i = 0; i < num_pairs; i++)
  {

    if (write_pair(kvs_table, keys[i], values[i]) != 0)
    {
      fprintf(stderr, "Failed to write keypair (%s,%s)\n", keys[i], values[i]);
    }
  }

  pthread_rwlock_unlock(&kvs_lock);

  return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut)
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  write(fdOut, "[", 1);

  pthread_rwlock_rdlock(&kvs_lock);

  for (size_t i = 0; i < num_pairs; i++)
  {
    char *result = read_pair(kvs_table, keys[i]);
    if (result == NULL)
    {
      write(fdOut, "(", 1);
      write(fdOut, keys[i], strlen(keys[i]));
      write(fdOut, ",KVSERROR)", 10);
    }
    else
    {
      write(fdOut, "(", 1);
      write(fdOut, keys[i], strlen(keys[i]));
      write(fdOut, ",", 1);
      write(fdOut, result, strlen(result));
      write(fdOut, ")", 1);
    }
    free(result);
  }

  pthread_rwlock_unlock(&kvs_lock);

  write(fdOut, "]\n", 2);

  return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut)
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }
  int aux = 0;

  pthread_rwlock_wrlock(&kvs_lock);

  for (size_t i = 0; i < num_pairs; i++)
  {
    if (delete_pair(kvs_table, keys[i]) != 0)
    {
      if (!aux)
      {

        write(fdOut, "[", 1);
        aux = 1;
      }

      write(fdOut, "(", 1);
      write(fdOut, keys[i], strlen(keys[i]));
      write(fdOut, ",KVSMISSING)", 12);
    }
  }

  pthread_rwlock_unlock(&kvs_lock);
  if (aux)
  {

    write(fdOut, "]\n", 2);
  }

  return 0;
}

void kvs_show(int fdOut)
{
  pthread_rwlock_rdlock(&kvs_lock);
  for (int i = 0; i < TABLE_SIZE; i++)
  {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL)
    {

      write(fdOut, "(", 1);
      write(fdOut, keyNode->key, strlen(keyNode->key));
      write(fdOut, ", ", 2);
      write(fdOut, keyNode->value, strlen(keyNode->value));
      write(fdOut, ")\n", 2);
      keyNode = keyNode->next;
    }
  }
  pthread_rwlock_unlock(&kvs_lock);
}

void kvs_show_bck(int fdOut)
{
  for (int i = 0; i < TABLE_SIZE; i++)
  {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL)
    {

      write(fdOut, "(", 1);
      write(fdOut, keyNode->key, strlen(keyNode->key));
      write(fdOut, ", ", 2);
      write(fdOut, keyNode->value, strlen(keyNode->value));
      write(fdOut, ")\n", 2);
      keyNode = keyNode->next;
    }
  }
}

void generateBackup(char *bckFilename)
{
  int fdOutput = open(bckFilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fdOutput < 0)
  {
    perror("Failed to create backup file");
    return;
  }

  kvs_show_bck(fdOutput);
  close(fdOutput);
}
int kvs_backup(char *input_filename)
{

  size_t len = strlen(input_filename);
  char bckFilename[len + MAX_STRING_SIZE];

  int counter = 1;
  char temp_bckFilename[len + MAX_STRING_SIZE];
  strcpy(temp_bckFilename, input_filename);
  temp_bckFilename[strlen(temp_bckFilename) - 4] = '\0';

  while (1)
  {
    sprintf(bckFilename, "%s-%d.bck", temp_bckFilename, counter);
    if (access(bckFilename, F_OK) == 0)
    {
      counter++;
    }
    else
    {
      break;
    }
  }

  sprintf(bckFilename, "%s-%d.bck", temp_bckFilename, counter);

  generateBackup(bckFilename);
  return 0;
}

void kvs_wait(unsigned int delay_ms)
{
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}