//*****************************************************************************
//
// persistent.c
//
// handles all the goings-on for persistent rooms. If a room is to be loaded,
// first check if it has a persistent copy on disk. Read that in. Otherwise,
// run the rproto as usual. When a persistent room's state changes, make sure
// it is saved to disk. When persistent rooms need to be loaded back up after
// a copyover or reboot, make sure that happens.
// 
//*****************************************************************************

#include "../mud.h"
#include "../utils.h"
#include "../world.h"
#include "../auxiliary.h"
#include "../storage.h"
#include "../room.h"
#include "../handler.h"
#include "../hooks.h"
#include "../event.h"
#include "../character.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../scripts/scripts.h"
#include "../scripts/pyroom.h"



//*****************************************************************************
// auxiliary data
//*****************************************************************************
typedef struct {
  bool      dirty; // do we need to be saved?
  bool persistent; // are we persistent or not?
  int    activity; // how many 'things' are going on in us? If activity is
                   // > 0, we have to make sure we force-load at startup
  time_t last_use; // the last time someone entered our room
} PERSISTENT_DATA;

PERSISTENT_DATA *newPersistentData(void) {
  PERSISTENT_DATA *data = malloc(sizeof(PERSISTENT_DATA));
  data->persistent      = FALSE;
  data->activity        = 0;
  data->last_use        = current_time;
  data->dirty           = FALSE;
  return data;
}

void deletePersistentData(PERSISTENT_DATA *data) {
  free(data);
}

void persistentDataCopyTo(PERSISTENT_DATA *from, PERSISTENT_DATA *to) {
  *to = *from;
}

PERSISTENT_DATA *persistentDataCopy(PERSISTENT_DATA *data) {
  PERSISTENT_DATA *newdata = newPersistentData();
  persistentDataCopyTo(data, newdata);
  return newdata;
}

STORAGE_SET *persistentDataStore(PERSISTENT_DATA *data) {
  STORAGE_SET *set = new_storage_set();
  store_bool(set, "persistent", data->persistent);
  store_int(set,  "activity",   data->activity);
  return set;
}

PERSISTENT_DATA *persistentDataRead(STORAGE_SET *set) {
  PERSISTENT_DATA *data = newPersistentData();
  data->persistent = read_bool(set, "persistent");
  data->activity   = read_int(set,  "activity");
  return data;
}



//*****************************************************************************
// local functions
//*****************************************************************************
LIST *p_to_save = NULL; // our list of persistent rooms to save to disk

// at 1 million rooms, this should mean 1000000 / (64 * 64) = 244 files/folder
#define WORLD_BINS 64



//*****************************************************************************
// interaction with the database of persistent rooms
//*****************************************************************************
bool persistentRoomExists(WORLD_DATA *world, const char *key) {
  static char fname[MAX_BUFFER];
  if(!*key)
    return FALSE;

  *fname = '\0';
  sprintf(fname, "%s/persistent/%lu/%lu/%s", 
	  worldGetPath(world),
	  pearson_hash8_1(key) % WORLD_BINS, 
	  pearson_hash8_2(key) % WORLD_BINS,
	  key);

  return file_exists(fname);
}

//
// store a room in the persistent database
void worldClearPersistentRoom(WORLD_DATA *world, const char *key) {
  static char fname[MAX_BUFFER];
  if(!*key)
    return;

  *fname = '\0';
  sprintf(fname, "%s/persistent/%lu/%lu/%s", 
	  worldGetPath(world),
	  pearson_hash8_1(key) % WORLD_BINS, 
	  pearson_hash8_2(key) % WORLD_BINS,
	  key);
  // log_string("Clearing persistent room, %s", key);

  if(file_exists(fname))
    unlink(fname);
}

//
// store a room in the persistent database
void worldStorePersistentRoom(WORLD_DATA *world, const char *key,
			      ROOM_DATA *room) {
  static char fname[MAX_BUFFER];
  static char  dir1[SMALL_BUFFER];
  static char  dir2[SMALL_BUFFER];
  if(!*key)
    return;

  unsigned long hash1 = pearson_hash8_1(key) % WORLD_BINS;
  unsigned long hash2 = pearson_hash8_2(key) % WORLD_BINS;
  *fname = '\0';
  *dir1  = '\0';
  *dir2  = '\0';

  // make sure our hash bins exist
  sprintf(dir1, "%s/persistent/%lu", 
	  worldGetPath(world), hash1);
  if(!dir_exists(dir1))
    mkdir(dir1, S_IRWXU | S_IRWXG);

  // and the second one as well
  sprintf(dir2, "%s/persistent/%lu/%lu", 
	  worldGetPath(world), hash1, hash2);
  if(!dir_exists(dir2))
    mkdir(dir2, S_IRWXU | S_IRWXG);

  // now, store the room
  sprintf(fname, "%s/persistent/%lu/%lu/%s", 
	  worldGetPath(world), hash1, hash2, key);
  STORAGE_SET *set = roomStore(room);
  storage_write(set, fname);
  storage_close(set);

  // log_string("stored persistent room :: %s", fname);
}

//
// pre-emptively, we're preparing for very large persistent world (1mil+rooms).
// Something like this, it would be real nice to have database storage for.
// Alas, we're using flat files... so we're going to have to do some pretty
// creative hashing, so the folders don't overflow and become impossible to
// access. make two layers of directories, each with 500 folders. That will
// give us 4 room files to a folder, for a 1mil room persistent world. 
ROOM_DATA *worldGetPersistentRoom(WORLD_DATA *world, const char *key) {
  static char fname[MAX_BUFFER];
  if(!*key)
    return NULL;

  *fname = '\0';
  sprintf(fname, "%s/persistent/%lu/%lu/%s", 
	  worldGetPath(world),
	  pearson_hash8_1(key) % WORLD_BINS, 
	  pearson_hash8_2(key) % WORLD_BINS,
	  key);

  if(!file_exists(fname))
    return NULL;
  else {
    // log_string("%-30s get persistent: %s", key, fname);
    STORAGE_SET *set = storage_read(fname);
    ROOM_DATA  *room = roomRead(set);
    storage_close(set);
    worldPutRoom(world, key, room);
    room_to_game(room);
    return room;
  }
}



//*****************************************************************************
// interaction with the persistent aux data
//*****************************************************************************
void roomUpdateLastUse(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  data->last_use = current_time;
}

time_t roomGetLastUse(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  return data->last_use;
}

void roomSetPersistent(ROOM_DATA *room, bool val) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");

  // if it was persistent before and not now, clear our database entry
  if(data->persistent == TRUE && val == FALSE)
    worldClearPersistentRoom(gameworld, roomGetClass(room));

  data->persistent = val;
}

bool roomIsPersistent(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  return data->persistent;
}

bool roomIsPersistentDirty(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  return data->dirty;
}

void roomSetPersistentDirty(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  data->dirty = TRUE;
}

void roomClearPersistentDirty(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  data->dirty = FALSE;
}

//
// add 'activity' to a persistent room. If a persistent room is active, make it
// automatically load at bootup, so the activity can continue
void roomAddActivity(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  data->activity++;
  data->last_use = current_time;

  // add us to the list of active rooms
  //***********
  // FINISH ME
  //***********
}

void roomRemoveActivity(ROOM_DATA *room) {
  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");
  data->activity--;

  // remove us from the list of active rooms
  if(data->activity == 0) {
    //***********
    // FINISH ME
    //***********
  }
}



//*****************************************************************************
// Python extensions
//*****************************************************************************

//
// prepare a persistent room to be saved to disc
PyObject *PyRoom_dirtyPersistence(PyObject *pyroom) {
  ROOM_DATA *room = PyRoom_AsRoom(pyroom);
  if(room == NULL) {
    PyErr_Format(PyExc_TypeError, "tried to dirty nonexistent room.");
    return NULL;
  }
  
  // if we're not persistent, ignore
  if(!roomIsPersistent(room))
    return Py_BuildValue("i", 0);
  else if(!roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
  return Py_BuildValue("");
}

//
// unload a persistent room from memory. Will not work if PCs are present.
PyObject *PyRoom_unloadPersistence(PyObject *pyroom) {
  ROOM_DATA *room = PyRoom_AsRoom(pyroom);
  if(room == NULL) {
    PyErr_Format(PyExc_TypeError, "tried to save nonexistent room.");
    return NULL;
  }

  // it's not pesistent
  if(!roomIsPersistent(room))
    return Py_BuildValue("i", 0);

  // does it contain a PC?
  LIST_ITERATOR *ch_i = newListIterator(roomGetCharacters(room));
  CHAR_DATA       *ch = NULL;
  bool       pc_found = FALSE;
  ITERATE_LIST(ch, ch_i) {
    if(!charIsNPC(ch)) {
      pc_found = TRUE;
      break;
    }
  } deleteListIterator(ch_i);

  if(pc_found)
    return Py_BuildValue("i", 0);
  worldStorePersistentRoom(gameworld, roomGetClass(room), room);
  extract_room(room);
  return Py_BuildValue("");
}


PyObject *PyRoom_getpersistent(PyObject *self, void *closure) {
  ROOM_DATA *room = PyRoom_AsRoom(self);
  if(room != NULL)  return Py_BuildValue("i", roomIsPersistent(room));
  else              return NULL;
}

int PyRoom_setpersistent(PyObject *self, PyObject *arg) {
  ROOM_DATA *room = PyRoom_AsRoom(self);
  if(room == NULL)  
    return -1;
  else if(arg == Py_True)
    roomSetPersistent(room, TRUE);
  else if(arg == Py_False)
    roomSetPersistent(room, FALSE);
  else
    return -1;
  return 0;
}



//*****************************************************************************
// hooks
//*****************************************************************************
void update_persistent_char_to_room(const char *info) {
  CHAR_DATA   *ch = NULL;
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &ch, &room);
  if(!charIsNPC(ch))
    roomUpdateLastUse(room);
  if(charIsNPC(ch) && roomIsPersistent(room) && !roomIsExtracted(room) &&
     !roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
}

void update_persistent_char_from_room(const char *info) {
  CHAR_DATA   *ch = NULL;
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &ch, &room);
  if(charIsNPC(ch) && roomIsPersistent(room) && !roomIsExtracted(room) && 
     !roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
}

void update_persistent_obj_to_room(const char *info) {
  OBJ_DATA   *obj = NULL;
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &obj, &room);
  if(roomIsPersistent(room) && !roomIsExtracted(room) &&
     !roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
}

void update_persistent_obj_from_room(const char *info) {
  OBJ_DATA   *obj = NULL;
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &obj, &room);
  if(roomIsPersistent(room)&& !roomIsExtracted(room)&&
     !roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
}

void update_persistent_obj_from_obj(const char *info) {
  OBJ_DATA       *obj = NULL;
  OBJ_DATA *container = NULL;
  ROOM_DATA     *root = NULL;
  hookParseInfo(info, &obj, &container);
  if(container == NULL || obj == NULL)
    return;

  root = objGetRootRoom(container);
  if(root == NULL)
    return;

  if(roomIsPersistent(root) && !roomIsExtracted(root) &&
     !roomIsPersistentDirty(root)) {
    listPut(p_to_save, root);
    roomSetPersistentDirty(root);
  }
}

void update_persistent_obj_to_obj(const char *info) {
  OBJ_DATA       *obj = NULL;
  OBJ_DATA *container = NULL;
  ROOM_DATA     *root = NULL;
  hookParseInfo(info, &obj, &container);
  if(container == NULL || obj == NULL)
    return;

  root = objGetRootRoom(container);
  if(root == NULL)
    return;

  if(roomIsPersistent(root) && !roomIsExtracted(root) &&
     !roomIsPersistentDirty(root)) {
    listPut(p_to_save, root);
    roomSetPersistentDirty(root);
  }
}

void update_persistent_room_from_game(const char *info) {
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &room);
  listRemove(p_to_save, room);

  // have we been replaced by a non-persistent room?
  ROOM_DATA *new_room = worldGetRoom(gameworld, roomGetClass(room));
  if(roomIsPersistent(room) && new_room != NULL && !roomIsPersistent(new_room))
    worldClearPersistentRoom(gameworld, roomGetClass(room));
}

void update_persistent_room_change(const char *info) {
  ROOM_DATA *room = NULL;
  hookParseInfo(info, &room);
  if(roomIsPersistent(room) && !roomIsExtracted(room) &&
     !roomIsPersistentDirty(room)) {
    listPut(p_to_save, room);
    roomSetPersistentDirty(room);
  }
}



//*****************************************************************************
// events
//*****************************************************************************

//
// save all of our pending persistent rooms to disc
void flush_persistent_rooms_event(void *owner, void *data, const char *arg) {
  ROOM_DATA *room = NULL;
  while( (room = listPop(p_to_save)) != NULL) {
    worldStorePersistentRoom(gameworld, roomGetClass(room), room);
    roomClearPersistentDirty(room);
  }
}

//
// every pulse, randomly sample our room table. If we find a persistent
// room that hasn't been active for awhile, unload it to disk so we aren't
// hogging up memory usage with a ton of unused rooms. Notably, this function
// kind of sucks because rooms get their UIDs from the same pool as objects
// and characters. That means a randomly generated UID is not always a room
// uid. It may also select room UIDs that have already been unloaded. What we
// really want to do is just sample a room from a known set of existing rooms.
// 
// This function has been disabled until it is improved a little.
//
void close_unused_rooms_event(void *owner, void *unused, const char *arg) {
  int top = top_uid();
  if(top == NOTHING)
    return;

  // randomly sample from the room table
  int  uid_to_try = (rand() % (top - START_UID)) + START_UID;
  ROOM_DATA *room = propertyTableGet(room_table, uid_to_try);
  if(room == NULL)
    return;

  PERSISTENT_DATA *data = roomGetAuxiliaryData(room, "persistent_data");

  // we've been inactive for more than 15 minutes, unload us
  if(data->persistent && data->activity == 0 && 
     difftime(current_time, roomGetLastUse(room)) > 60 * 15)
    extract_room(room);
}



//*****************************************************************************
// initialization
//*****************************************************************************
void init_persistent(void) {
  p_to_save = newList();

  auxiliariesInstall("persistent_data", 
		     newAuxiliaryFuncs(AUXILIARY_TYPE_ROOM,
				       newPersistentData, deletePersistentData,
				       persistentDataCopyTo, persistentDataCopy,
				       persistentDataStore,persistentDataRead));

  // start our flushing of persistent rooms that need to be saved
  start_update(NULL, 1, flush_persistent_rooms_event, NULL,NULL,NULL);

  //
  // disabled until a better implementation is written.
  //
  // start_update(NULL, 1, close_unused_rooms_event,     NULL,NULL,NULL);

  // listen for objects and characters entering 
  // or leaving rooms. Update those rooms' statuses
  hookAdd("char_to_room",   update_persistent_char_to_room);
  hookAdd("char_from_room", update_persistent_char_from_room);
  hookAdd("obj_to_room",    update_persistent_obj_to_room);
  hookAdd("obj_from_room",  update_persistent_obj_from_room);
  hookAdd("obj_from_obj",   update_persistent_obj_from_obj);
  hookAdd("obj_to_obj",     update_persistent_obj_to_obj);
  hookAdd("room_from_game", update_persistent_room_from_game);
  hookAdd("room_change",    update_persistent_room_change);
  
  // add accessibility to Python
  /*
  PyRoom_addMethod("add_activity",  PyRoom_addActivity,   METH_NOARGS, NULL);
  PyRoom_addMethod("rem_activity",  PyRoom_remActivity,   METH_NOARGS, NULL);
  */
  PyRoom_addMethod("dirty",         PyRoom_dirtyPersistence,  METH_NOARGS,NULL);
  PyRoom_addMethod("unload",        PyRoom_unloadPersistence, METH_NOARGS,NULL);
  PyRoom_addGetSetter("persistent", 
		      PyRoom_getpersistent,     
		      PyRoom_setpersistent, NULL);
}
