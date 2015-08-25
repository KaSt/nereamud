#ifndef STATS_H
#define STATS_H
//*****************************************************************************
//
// stats.h
//
// defines a couple datastructures for storing, setting, and modifying the 
// combat stats of characters. Traditionally, stats can range between 0 and
// some maximum value that depends on the character. Values that can start at 0
// and increase indefinitely on a character.
//
//*****************************************************************************

//
// prepare stats for use by the mud
void init_stats(void);

//
// creates a new type of statistic. 
// When a character is created, his base stats are always the default val
void stat_add(const char *name);

//
// sets the default value for stats when a char is created
void stat_set_default(int val);

//
// return TRUE if a statistic exists with the given name
bool stat_exists(const char *name);

//
// returns a list of the current stats available. List and contents must be
// deleted after use (try: deleteListWith(list, free);)
LIST *get_stats(void);

//
// return the current value of the character's statistic. Return 0 if the
// supplied name is not a valid statistic
int charGetStat(CHAR_DATA *ch, const char *stat);

//
// sets the current value of the character's statistic.
void charSetStat(CHAR_DATA *ch, const char *stat, int val);

//
// same deals as charGetStat and charSet stat, but for the maximum value a
// character's stat can be and the base value (sans positive or negative
// modifications on the max stat).
int  charGetBaseStat(CHAR_DATA *ch, const char *stat);
void charSetBaseStat(CHAR_DATA *ch, const char *stat, int val);
int  charGetMaxStat (CHAR_DATA *ch, const char *stat);
void charModifyMaxStat(CHAR_DATA *ch, const char *stat, int amount);

//
// set the character's stat to be the value of that stat's maxiumum
void charResetStat(CHAR_DATA *ch, const char *stat);

//
// reset's the character's max stat to its base value
void charResetMaxStat(CHAR_DATA *ch, const char *stat);

//
// sets the last time the stat was used to now
void charUseStat    (CHAR_DATA *ch, const char *stat);

//
// gets the last time we used the stat
long charGetStatUsed(CHAR_DATA *ch, const char *stat);

#endif // STATS_H
