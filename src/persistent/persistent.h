#ifndef PERSISTENT_H
#define PERSISTENT_H
//*****************************************************************************
//
// persistent.h
//
// handles all the goings-on for persistent rooms. If a room is to be loaded,
// first check if it has a persistent copy on disk. Read that in. Otherwise,
// run the rproto as usual. When a persistent room's state changes, make sure
// it is saved to disk. When persistent rooms need to be loaded back up after
// a copyover or reboot, make sure that happens.
// 
//*****************************************************************************

//
// make persistent rooms ready
void init_persistent(void);

//
// make a room persistent or not
void roomSetPersistent(ROOM_DATA *room, bool val);
bool roomIsPersistent(ROOM_DATA *room);

//
// add 'activity' to a persistent room. If a persistent room is active, make it
// automatically load at bootup, so the activity can continue
void roomAddActivity(ROOM_DATA *room);
void roomRemoveActivity(ROOM_DATA *room);

//
// return a persistent room if it exists, NULL otherwise
ROOM_DATA *worldGetPersistentRoom(WORLD_DATA *world, const char *key);

//
// write a room to disk. Must be called after a room is updated in some way
void worldStorePersistentRoom(WORLD_DATA *world, const char *key, 
			      ROOM_DATA *room);

//
// return whether a persistent room of the given name exists
bool persistentRoomExists(WORLD_DATA *world, const char *key);

#endif // PERSISTENT_H
