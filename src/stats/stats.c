//*****************************************************************************
//
// stats.c
//
// defines a couple datastructures for storing, setting, and modifying the 
// combat stats of characters. Traditionally, stats can range between 0 and
// some maximum value that depends on the character. Values that can start at 0
// and increase indefinitely on a character.
//
//*****************************************************************************
#include <sys/time.h>

#include "../mud.h"
#include "../character.h"
#include "../utils.h"
#include "../storage.h"
#include "../auxiliary.h"

#include "stats.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../scripts/scripts.h"
#include "../scripts/pychar.h"
#include "../scripts/pymudsys.h"



//*****************************************************************************
// local datastructures and variables
//*****************************************************************************

// the default value for base stats
int stat_default_val = 0;

// a list of all the valid statistic names
LIST *stat_names = NULL;

typedef struct {
  int       curr; // the current value of the character's stat
  int       base; // the base (unmodified) max for the character
  int        mod; // how much is our max modified by the offset of our base?
  long last_used; // the last time we used this statistic
} STAT_DATA;

typedef struct {
  MAP *stat_map;  // a mapping from name:STAT_DATA
} STAT_AUX_DATA;

STAT_DATA *newStatData() {
  STAT_DATA *data = calloc(1, sizeof(STAT_DATA));
  // just temporary
  data->curr = data->base = stat_default_val;
  data->mod  = 0;
  data->last_used = time(0);
  return data;
}

void deleteStatData(STAT_DATA *data) {
  free(data);
}

void statDataCopyTo(STAT_DATA *from, STAT_DATA *to) {
  *to = *from;
}

STAT_DATA *statDataCopy(STAT_DATA *data) {
  STAT_DATA *newstat = newStatData();
  statDataCopyTo(data, newstat);
  return newstat;
}

STORAGE_SET *statDataStore(STAT_DATA *data) {
  STORAGE_SET *set = new_storage_set();
  store_int(set,  "curr",      data->curr);
  store_int(set,  "base",      data->base);
  store_int(set,  "mod",       data->mod);
  store_long(set, "last_used", data->last_used);
  return set;
}

STAT_DATA *statDataRead(STORAGE_SET *set) {
  STAT_DATA *data = newStatData();
  data->curr      = read_int (set, "curr");
  data->base      = read_int (set, "base");
  data->mod       = read_int (set, "mod");
  data->last_used = read_long(set, "last_used");
  return data;
}



//*****************************************************************************
// functions for creating, saving, and loading auxiliary data
//*****************************************************************************
STAT_AUX_DATA *
newStatAuxData() {
  STAT_AUX_DATA *data = malloc(sizeof(STAT_AUX_DATA));
  data->stat_map = newMap(string_hash, strcasecmp);

  // fill us up with entries for all the stat names
  LIST_ITERATOR *name_i = newListIterator(stat_names);
  char            *name = NULL;
  ITERATE_LIST(name, name_i)
    mapPut(data->stat_map, name, newStatData());
  deleteListIterator(name_i);
  return data;
}

void
deleteStatAuxData(STAT_AUX_DATA *data) {
  // iterate across our list of stat names, and 
  // delete the entry in the stat table for each name
  MAP_ITERATOR *map_i = newMapIterator(data->stat_map);
  STAT_DATA     *stat = NULL;
  const char    *name = NULL;
  ITERATE_MAP(name, stat, map_i)
    deleteStatData(stat);
  deleteMapIterator(map_i);
  deleteMap(data->stat_map);
  free(data);
}

void
statAuxDataCopyTo(STAT_AUX_DATA *from, STAT_AUX_DATA *to) {
  // first, delete all of our old data if it's needed
  if(mapSize(to->stat_map) > 0) {
    MAP_ITERATOR *map_i = newMapIterator(to->stat_map);
    STAT_DATA     *stat = NULL;
    const char    *name = NULL;
    ITERATE_MAP(name, stat, map_i)
      deleteStatData(mapRemove(to->stat_map, name));
    deleteMapIterator(map_i);
  }

  // now, copy everything
  MAP_ITERATOR  *map_i = newMapIterator(from->stat_map);
  STAT_DATA     *stats = NULL;
  const char     *name = NULL;
  ITERATE_MAP(name, stats, map_i)
    mapPut(to->stat_map, name, statDataCopy(stats));
  deleteMapIterator(map_i);
}

STAT_AUX_DATA *
statAuxDataCopy(STAT_AUX_DATA *data) {
  STAT_AUX_DATA *newdata = malloc(sizeof(STAT_AUX_DATA));
  newdata->stat_map = newMap(string_hash, strcasecmp);
  statAuxDataCopyTo(data, newdata);
  return newdata;
}

STORAGE_SET *statAuxDataStore(STAT_AUX_DATA *data) {
  STORAGE_SET        *set = new_storage_set();
  STORAGE_SET_LIST *stats = new_storage_list();
  store_list(set, "stats", stats);

  // iterate across all of our stats, and each one to 
  // the stat list that we will be storing in the storage set
  MAP_ITERATOR *map_i = newMapIterator(data->stat_map);
  STAT_DATA     *stat = NULL;
  const char    *name = NULL;
  ITERATE_MAP(name, stat, map_i) {
    STORAGE_SET *one_stat = new_storage_set();
    store_string(one_stat, "key", name);
    store_set   (one_stat, "val", statDataStore(stat));
    storage_list_put(stats, one_stat);
  } deleteMapIterator(map_i);

  return set;
}

STAT_AUX_DATA *statAuxDataRead(STORAGE_SET *set) {
  STAT_AUX_DATA     *data = newStatAuxData();
  STORAGE_SET_LIST *stats = read_list(set, "stats");
  STORAGE_SET   *one_stat = NULL;

  // parse each stat, and its corresponding values
  while( (one_stat = storage_list_next(stats)) != NULL) {
    STAT_DATA *stat = statDataRead(read_set(one_stat, "val"));
    STAT_DATA *old  = mapGet(data->stat_map, read_string(one_stat, "key"));
    if(old) statDataCopyTo(stat, old);
    deleteStatData(stat);
  }

  return data;
}



//*****************************************************************************
// python extensions
//*****************************************************************************
PyObject *PyChar_GetStat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  if(!PyArg_ParseTuple(args, "s", &stat)) {
    PyErr_Format(PyExc_TypeError, "Stat name must be supplied.");
    return NULL;
  }

  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character does not exist.");
    return NULL;
  }

  return Py_BuildValue("i", charGetStat(ch, stat));
}

PyObject *PyChar_GetBaseStat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  if(!PyArg_ParseTuple(args, "s", &stat)) {
    PyErr_Format(PyExc_TypeError, "Stat name must be supplied.");
    return NULL;
  }

  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character does not exist.");
    return NULL;
  }

  return Py_BuildValue("i", charGetBaseStat(ch, stat));
}

PyObject *PyChar_GetMaxStat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  if(!PyArg_ParseTuple(args, "s", &stat)) {
    PyErr_Format(PyExc_TypeError, "Stat name must be supplied.");
    return NULL;
  }

  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character does not exist.");
    return NULL;
  }

  return Py_BuildValue("i", charGetMaxStat(ch, stat));
}

PyObject *PyChar_SetStat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  int   amnt = 0;
  if(!PyArg_ParseTuple(args, "si", &stat, &amnt)) {
    PyErr_Format(PyExc_TypeError, "Stat name and amount must be supplied.");
    return NULL;
  }

  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character does not exist.");
    return NULL;
  }

  // increase our stat
  charSetStat(ch, stat, amnt);
  return Py_BuildValue("i", 1);
}

PyObject *PyChar_SetBaseStat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  int   amnt = 0;
  if(!PyArg_ParseTuple(args, "si", &stat, &amnt)) {
    PyErr_Format(PyExc_TypeError, "Stat name and amount must be supplied.");
    return NULL;
  }

  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character does not exist.");
    return NULL;
  }

  // increase our stat
  charSetBaseStat(ch, stat, amnt);
  return Py_BuildValue("i", 1);
}

//
// adds a new type of statistic to the game, with a base value
PyObject *PyMudSys_add_stat(PyObject *self, PyObject *args) {
  char *stat = NULL;
  if(!PyArg_ParseTuple(args, "s", &stat)) {
    PyErr_Format(PyExc_TypeError, "A stat name must be supplied.");
    return NULL;
  }

  stat_add(stat);
  return Py_BuildValue("i", 1);
}

//
// Returns whether or not a stat with the given name exists
PyObject *PyMudSys_stat_exists(PyObject *self, PyObject *args) {
  char *stat = NULL;
  if(!PyArg_ParseTuple(args, "s", &stat)) {
    PyErr_Format(PyExc_TypeError, "A stat name must be supplied.");
    return NULL;
  }

  return Py_BuildValue("i", stat_exists(stat));
}



//*****************************************************************************
// implementation of stats.h
//*****************************************************************************
void init_stats(void) {
  stat_names = newList();
  auxiliariesInstall("stat_aux_data",
		     newAuxiliaryFuncs(AUXILIARY_TYPE_CHAR,
				       newStatAuxData, deleteStatAuxData,
				       statAuxDataCopyTo, statAuxDataCopy,
				       statAuxDataStore, statAuxDataRead));

  // get our python extensions set up
  PyChar_addMethod("get_stat",     PyChar_GetStat,       METH_VARARGS, NULL);
  PyChar_addMethod("get_base_stat",PyChar_GetBaseStat,   METH_VARARGS, NULL);
  PyChar_addMethod("set_stat",     PyChar_SetStat,       METH_VARARGS, NULL);
  PyChar_addMethod("set_base_stat",PyChar_SetBaseStat,   METH_VARARGS, NULL);
  PyChar_addMethod("get_max_stat", PyChar_GetMaxStat,    METH_VARARGS, NULL);
  PyMudSys_addMethod("add_stat",   PyMudSys_add_stat,    METH_VARARGS, NULL);
  PyMudSys_addMethod("stat_exists",PyMudSys_stat_exists, METH_VARARGS, NULL);
}

void stat_add(const char *name) {
  if(!stat_exists(name))
    listPutWith(stat_names, strdup(name), strcasecmp);
}

LIST *get_stats(void) {
  return listCopyWith(stat_names, strdup);
}

void stat_set_default(int val) {
  stat_default_val = val;
}

bool stat_exists(const char *name) {
  return (listGetWith(stat_names, name, strcasecmp) != NULL);
}

int charGetStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  return (stats ? stats->curr : 0);
}

void charSetStat(CHAR_DATA *ch, const char *stat, int val) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->curr = val;
}

int charGetMaxStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats == NULL)
    return 0;
  else {
    int max = stats->base + stats->mod;
    return (max <= 0 ? 0 : max);
  }
}

void charModifyMaxStat(CHAR_DATA *ch, const char *stat, int amount) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->mod += amount;
}

int charGetBaseStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  return (stats ? stats->base : 0);
}

void charSetBaseStat(CHAR_DATA *ch, const char *stat, int val) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->base = val;
}

void charResetStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->curr = charGetMaxStat(ch, stat);
}

void charResetMaxStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->mod = 0;
}

void charUseStat(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  if(stats != NULL) stats->last_used = time(0);
}

long charGetStatUsed(CHAR_DATA *ch, const char *stat) {
  STAT_AUX_DATA *stat_aux = charGetAuxiliaryData(ch, "stat_aux_data");
  STAT_DATA        *stats = mapGet(stat_aux->stat_map, stat);
  return (stats ? stats->last_used : 0);
}
