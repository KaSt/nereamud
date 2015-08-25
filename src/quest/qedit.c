//*****************************************************************************
//
// qedit.c
//
// An OLC tool for quests. Allows builders to create and alter the info for
// quests. Works in a similar fashion to redit, medit, oedit.
//
//*****************************************************************************
#include "../mud.h"
#include "../utils.h"
#include "../world.h"
#include "../zone.h"
#include "../character.h"
#include "../socket.h"
#include "../prototype.h"
#include "../room.h"
#include "quest.h"
#include "qedit.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../olc2/olc.h"
#include "../editor/editor.h"
#include "../scripts/scripts.h"
#include "../scripts/script_editor.h"



//*****************************************************************************
// olc functions for quest objectives
//*****************************************************************************

#define QOEDIT_TYPE             1
#define QOEDIT_DESC             2
#define QOEDIT_KILL_ENEMY       3
#define QOEDIT_KILL_TIMES       4
#define QOEDIT_GIVE_RECEIVER    5
#define QOEDIT_GIVE_ITEM        6
#define QOEDIT_GIVE_COUNT       7
#define QOEDIT_APPROACH_PERSON  8

const char *qoedit_types[] = {
  "kill",
  "give",
  "approach",
  "indefinite",
  NULL,
};

//
// returns the name of the quest objective type for the given number
const char *qoeditGetName(int num) {
  return qoedit_types[num];
}

//
// keeps track of how many quest objective types we can edit in qedit
int num_qoedit_types() {
  static int num = -1;
  if(num == -1) {
    do {
      num++;
    } while(qoedit_types[num] != NULL);
  }
  return num;
}

//
// display the qedit menu for the approach objective
void qoedit_approach_menu(SOCKET_DATA *sock, QUEST_OBJECTIVE *data) {
  send_to_socket(sock,
		 "{g3) person  : {c%s\r\n",
		 questObjectiveGetVar(data, "person"));
}

//
// display the qedit menu for kill objectives
void qoedit_kill_menu(SOCKET_DATA *sock, QUEST_OBJECTIVE *data) {
  send_to_socket(sock, 
		 "{g3) enemy   : {c%s\r\n"
		 "{g4) times   : {c%s\r\n",
		 questObjectiveGetVar(data, "enemy"),
		 questObjectiveGetVar(data, "times"));
}

//
// display the qedit menu for give objectives
void qoedit_give_menu(SOCKET_DATA *sock, QUEST_OBJECTIVE *data) {
  send_to_socket(sock, 
		 "{g3) person  : {c%s\r\n"
		 "{g4) item    : {c%s\r\n"
		 "{g5) count   : {c%s\r\n",
		 questObjectiveGetVar(data, "person"),
		 questObjectiveGetVar(data, "item"),
		 questObjectiveGetVar(data, "count"));
}

void qoedit_menu(SOCKET_DATA *sock, QUEST_OBJECTIVE *data) {
  send_to_socket(sock, 
		 "{g1) type    : {c%s\r\n"  
		 "{g2) desc    : {c%s\r\n", 
		 questObjectiveGetType(data), questObjectiveGetDesc(data));
  if(!strcasecmp("kill", questObjectiveGetType(data)))
    qoedit_kill_menu(sock, data);
  else if(!strcasecmp("give", questObjectiveGetType(data)))
    qoedit_give_menu(sock, data);
  else if(!strcasecmp("approach", questObjectiveGetType(data)))
    qoedit_approach_menu(sock, data);
  send_to_socket(sock, "{n");
}

int qoedit_chooser(SOCKET_DATA *sock, QUEST_OBJECTIVE *data,const char *option){
  switch(toupper(*option)) {
  case '1':
    olc_display_table(sock, qoeditGetName, num_qoedit_types(), 1);
    send_to_socket(sock, "Enter a choice: ");
    return QOEDIT_TYPE;
  case '2':
    send_to_socket(sock, "Enter a description of the objective: ");
    return QOEDIT_DESC;
  case '3':
    if(!strcasecmp("kill", questObjectiveGetType(data))) {
      send_to_socket(sock, "Enter an enemy to kill: ");
      return QOEDIT_KILL_ENEMY;
    }
    else if(!strcasecmp("give", questObjectiveGetType(data))) {
      send_to_socket(sock, "Who must the items be given to: ");
      return QOEDIT_GIVE_RECEIVER;
    }
    else if(!strcasecmp("approach", questObjectiveGetType(data))) {
      send_to_socket(sock, "Who must be approached: ");
      return QOEDIT_APPROACH_PERSON;
    }
    else
      return MENU_CHOICE_INVALID;
  case '4':
    if(!strcasecmp("kill", questObjectiveGetType(data))) {
      send_to_socket(sock, "Enter a number of enemies to kill: ");
      return QOEDIT_KILL_TIMES;
    }
    else if(!strcasecmp("give", questObjectiveGetType(data))) {
      send_to_socket(sock, "Enter the item to give: ");
      return QOEDIT_GIVE_ITEM;
    }
    else
      return MENU_CHOICE_INVALID;
  case '5':
    if(!strcasecmp("give", questObjectiveGetType(data))) {
      send_to_socket(sock, "How many copies of the item must be given: ");
      return QOEDIT_GIVE_COUNT;
    }
    else
      return MENU_CHOICE_INVALID;
  default:
    return MENU_CHOICE_INVALID;
  }
}

bool qoedit_parser(SOCKET_DATA *sock, QUEST_OBJECTIVE *data, int choice, 
		  const char *arg) {
  switch(choice) {
  case QOEDIT_TYPE: {
    if(!isdigit(*arg))
      return FALSE;
    else {
      int val = atoi(arg);
      if(val < 0 || val >= num_qoedit_types())
	return FALSE;
      else {
	questObjectiveClearVars(data);
	questObjectiveSetType(data, qoedit_types[val]);

	// set our default values
	if(!strcasecmp("kill", qoedit_types[val])) {
	  questObjectiveSetVar(data, "enemy", "");
	  questObjectiveSetVar(data, "times", "0");
	}
	else if(!strcasecmp("give", qoedit_types[val])) {
	  questObjectiveSetVar(data, "person", "");
	  questObjectiveSetVar(data, "item",   "");
	  questObjectiveSetVar(data, "count",  "0");
	}
	else if(!strcasecmp("approach", qoedit_types[val])) {
	  questObjectiveSetVar(data, "person", "");
	}
	return TRUE;
      }
    }
  }
  case QOEDIT_DESC:
    questObjectiveSetDesc(data, arg);
    return TRUE;
  case QOEDIT_KILL_ENEMY:
    questObjectiveSetVar(data, "enemy", arg);
    return TRUE;
  case QOEDIT_KILL_TIMES:
    if(!isdigit(*arg))
      return FALSE;
    else {
      questObjectiveSetVar(data, "times", arg);
      return TRUE;
    }
  case QOEDIT_GIVE_RECEIVER:
    questObjectiveSetVar(data, "person", arg);
    return TRUE;
  case QOEDIT_GIVE_ITEM:
    questObjectiveSetVar(data, "item",  arg);
    return TRUE;
  case QOEDIT_GIVE_COUNT:
    if(!isdigit(*arg))
      return FALSE;
    else {
      questObjectiveSetVar(data, "count", arg);
      return TRUE;
    }
  case QOEDIT_APPROACH_PERSON:
    questObjectiveSetVar(data, "person", arg);
    return TRUE;
  default:
    return FALSE;
  }
}



//*****************************************************************************
// olc functions for quest stages
//*****************************************************************************

#define QSEDIT_NAME               1
#define QSEDIT_EDIT_OBJECTIVE     2
#define QSEDIT_DELETE_OBJECTIVE   3

//
// displays info for one objective to a socket
void disp_one_objective(SOCKET_DATA *sock, QUEST_OBJECTIVE *ob) {
  // need to get this, for seeing our locale
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  if(!strcasecmp("kill", questObjectiveGetType(ob))) {
    PROTO_DATA *mproto = 
      worldGetType(gameworld, "mproto", 
		   get_fullkey_relative(questObjectiveGetVar(ob, "enemy"),
					get_key_locale(questGetKey(quest))));
    send_to_socket(sock, "{cKill %s %s times.\r\n",
		   (mproto ? protoGetKey(mproto) : "{rNOBODY{c"),
		   questObjectiveGetVar(ob, "times"));
  }
  else if(!strcasecmp("give", questObjectiveGetType(ob))) {
    PROTO_DATA *mproto = 
      worldGetType(gameworld, "mproto", 
		   get_fullkey_relative(questObjectiveGetVar(ob, "person"),
					get_key_locale(questGetKey(quest))));
    PROTO_DATA *oproto = 
      worldGetType(gameworld, "oproto", 
		   get_fullkey_relative(questObjectiveGetVar(ob, "item"),
					get_key_locale(questGetKey(quest))));
    send_to_socket(sock, "{cGive %s copies of %s to %s.\r\n",
		   questObjectiveGetVar(ob, "count"),
		   (oproto ? protoGetKey(oproto) : "{rNOTHING{c"),
		   (mproto ? protoGetKey(mproto) : "{rNOBODY{c"));
  }
  else if(!strcasecmp("approach", questObjectiveGetType(ob))) {
    PROTO_DATA *mproto = 
      worldGetType(gameworld, "mproto", 
		   get_fullkey_relative(questObjectiveGetVar(ob, "person"),
					get_key_locale(questGetKey(quest))));
    send_to_socket(sock, "{cApproach %s about your deeds.\r\n",
		   (mproto ? protoGetKey(mproto) : "{rNOBODY{c"));
  }
  else if(!strcasecmp("indefinite", questObjectiveGetType(ob))) {
    send_to_socket(sock, "{cNo specific goal for this objective.\r\n");
  }
  else
    send_to_socket(sock, "{cUnknown objective, {r%s{c.\r\n", 
		   questObjectiveGetType(ob));
}

void qsedit_menu(SOCKET_DATA *sock, QUEST_STAGE *data) {
  send_to_socket(sock, 
		 "{g1) Name\r\n"
		 "{c%s\r\n"
		 "{g2) End Script\r\n",
		 questStageGetName(data));
  script_display(sock, questStageGetEndScript(data), FALSE);
  send_to_socket(sock, "\r\n{wObjectives:\r\n");
  LIST_ITERATOR *ob_i = newListIterator(questStageGetObjectives(data));
  QUEST_OBJECTIVE *ob = NULL;
  int i = 0;
  ITERATE_LIST(ob, ob_i) {
    send_to_socket(sock, "  {g%d) ", i);
    disp_one_objective(sock, ob);
    i++;
  } deleteListIterator(ob_i);
  send_to_socket(sock, 
		 "\r\n"
		 "  {gE) Edit objective\r\n"
		 "  {gN) New objective\r\n"
		 "  {gD) Delete objective\r\n");
}

int qsedit_chooser(SOCKET_DATA *sock, QUEST_STAGE *data, const char *option) {
  switch(toupper(*option)) {
  case '1':
    send_to_socket(sock, "Enter the name of the stage: ");
    return QSEDIT_NAME;
  case '2':
    socketStartEditor(sock, script_editor, questStageGetEndScriptBuf(data));
    return MENU_NOCHOICE;
  case 'N': {
    QUEST_OBJECTIVE *ob = newQuestObjective();
    questStageAddObjective(data, ob);
    do_olc(sock, qoedit_menu, qoedit_chooser, qoedit_parser,
	   NULL, NULL, NULL, NULL, ob);
    return MENU_NOCHOICE;
  }
  case 'E':
    send_to_socket(sock, "Which objective do you want to edit: ");
    return QSEDIT_EDIT_OBJECTIVE;
  case 'D':
    if(listSize(questStageGetObjectives(data)) == 0)
      return MENU_CHOICE_INVALID;
    send_to_socket(sock, "Which objective do you want to delete: ");
    return QSEDIT_DELETE_OBJECTIVE;
  default:
    return MENU_CHOICE_INVALID;
  }
}

bool qsedit_parser(SOCKET_DATA *sock, QUEST_STAGE *data, int choice, 
		  const char *arg) {
  switch(choice) {
  case QSEDIT_NAME:
    questStageSetName(data, arg);
    return TRUE;
  case QSEDIT_EDIT_OBJECTIVE:
    if(!isdigit(*arg) || listSize(questStageGetObjectives(data)) == 0)
      return FALSE;
    else {
      QUEST_OBJECTIVE *ob = listGet(questStageGetObjectives(data), atoi(arg));
      if(ob == NULL)
	return FALSE;
      else {
	do_olc(sock, qoedit_menu, qoedit_chooser, qoedit_parser,
	       NULL, NULL, NULL, NULL, ob);
	return TRUE;
      }
    }
  case QSEDIT_DELETE_OBJECTIVE:
    if(!isdigit(*arg))
      return FALSE;
    else {
      QUEST_OBJECTIVE *ob = questStageRemoveObjectiveNum(data, atoi(arg));
      if(ob != NULL)
	deleteQuestObjective(ob);
      return TRUE;
    }
  default:
    return FALSE;
  }
}



//*****************************************************************************
// olc functions for quests
//*****************************************************************************
#define QEDIT_NAME          1
#define QEDIT_EDIT_STAGE    2
#define QEDIT_DELETE_STAGE  3

void qedit_menu(SOCKET_DATA *sock, QUEST_DATA *data) {
  send_to_socket(sock,
		 "{y[{c%s{y]\r\n"
		 "{g1) Name\r\n"
		 "{c%s\r\n"
		 "{g2) Description\r\n"
		 "{c%s\r\n", 
		 questGetKey(data), questGetName(data), questGetDesc(data));
  send_to_socket(sock, "{wStages:\r\n");
  LIST_ITERATOR *st_i = newListIterator(questGetStages(data));
  QUEST_STAGE     *st = NULL;
  int i = 0;
  ITERATE_LIST(st, st_i) {
    send_to_socket(sock, "  {g%d) {c%s\r\n", i, questStageGetName(st));
    i++;
  } deleteListIterator(st_i);
  send_to_socket(sock,
		 "\r\n"
		 "  {gE) Edit Stage\r\n"
		 "  {gN) New Stage\r\n"
		 "  {gD) Delete Stage\r\n");
}

int qedit_chooser(SOCKET_DATA *sock, QUEST_DATA *data, const char *option) {
  switch(toupper(*option)) {
  case '1':
    send_to_socket(sock, "Enter new name for the quest: ");
    return QEDIT_NAME;
  case '2':
    socketStartEditor(sock, text_editor, questGetDescBuf(data));
    return MENU_NOCHOICE;
  case 'E':
    if(listSize(questGetStages(data)) == 0)
      return MENU_CHOICE_INVALID;
    send_to_socket(sock, "Which stage do you want to edit: ");
    return QEDIT_EDIT_STAGE;
  case 'N': {
    QUEST_STAGE *stage = newQuestStage();
    questAddStage(data, stage);
    do_olc(sock, qsedit_menu, qsedit_chooser, qsedit_parser,
	   NULL, NULL, NULL, NULL, stage);
    return MENU_NOCHOICE;
  }
  case 'D':
    if(listSize(questGetStages(data)) == 0)
      return MENU_CHOICE_INVALID;
    send_to_socket(sock, "Which stage do you want to delete: ");
    return QEDIT_DELETE_STAGE;
  default:
    return MENU_CHOICE_INVALID;
  }
}

bool qedit_parser(SOCKET_DATA *sock, QUEST_DATA *data, int choice, 
		  const char *arg) {
  switch(choice) {
  case QEDIT_NAME:
    questSetName(data, arg);
    return TRUE;
  case QEDIT_EDIT_STAGE:
    if(!isdigit(*arg))
      return FALSE;
    else {
      QUEST_STAGE *stage = listGet(questGetStages(data), atoi(arg));
      if(stage == NULL)
	return FALSE;
      else {
	do_olc(sock, qsedit_menu, qsedit_chooser, qsedit_parser,
	       NULL, NULL, NULL, NULL, stage);
	return TRUE;
      }
    }
  case QEDIT_DELETE_STAGE:
    if(!isdigit(*arg))
      return FALSE;
    else {
      QUEST_STAGE *stage = questRemoveStageNum(data, atoi(arg));
      if(stage != NULL)
	deleteQuestStage(stage);
      return TRUE;
    }
  default:
    return FALSE;
  }
}



//*****************************************************************************
// builder commands
//*****************************************************************************
void olc_save_quest(QUEST_DATA *quest) {
  worldSaveType(gameworld, "quest", questGetKey(quest));
}

COMMAND(cmd_qedit) {
  ZONE_DATA    *zone = NULL;
  QUEST_DATA  *quest = NULL;

  // we need a key
  if(!arg || !*arg)
    send_to_char(ch, "What is the name of the quest you want to edit?\r\n");
  else {
    char locale[SMALL_BUFFER];
    char   name[SMALL_BUFFER];
    if(!parse_worldkey_relative(ch, arg, name, locale))
      send_to_char(ch, "Which quest are you trying to edit?\r\n");
    // make sure we can edit the zone
    else if((zone = worldGetZone(gameworld, locale)) == NULL)
      send_to_char(ch, "No such zone exists.\r\n");
    else if(!canEditZone(zone, ch))
      send_to_char(ch, "You are not authorized to edit that zone.\r\n");
    else {
      // try to pull up the prototype
      quest = worldGetType(gameworld, "quest", get_fullkey(name, locale));
      if(quest == NULL) {
	quest = newQuest();
	questSetName(quest, "An unfinished quest");
	questSetDesc(quest, "Something unfinished has happened, "
		     "and now you must fix it.\r\n");
	worldPutType(gameworld, "quest", get_fullkey(name, locale), quest);
      }
      
      do_olc(charGetSocket(ch), qedit_menu, qedit_chooser, qedit_parser,
 	     questCopy, questCopyTo, deleteQuest, olc_save_quest, quest);
    }
  }
}

// this is used for the header when printing out zone proto info
#define QUEST_LIST_HEADER \
"Name                                                  "

const char *qlist_list_name(QUEST_DATA *quest) {
  static char buf[SMALL_BUFFER];
  sprintf(buf, "%-54s", questGetName(quest));
  return buf;
}

COMMAND(cmd_qlist) {
  do_list(ch, (arg&&*arg?arg:get_key_locale(roomGetClass(charGetRoom(ch)))),
	  "quest", QUEST_LIST_HEADER, qlist_list_name);
}

COMMAND(cmd_qrename) {
  char *from = NULL, *to = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word word", &from, &to))
    return;
  do_rename(ch, "quest", from, to);
}

COMMAND(cmd_qdelete) {
  char *name = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word", &name))
    return;
  do_delete(ch, "quest", deleteQuest, name);
}



//*****************************************************************************
// initialization
//*****************************************************************************
void init_qedit(void) {
  add_cmd("qedit",   NULL, cmd_qedit,   "builder", TRUE);
  add_cmd("qlist",   NULL, cmd_qlist,   "builder", TRUE);
  add_cmd("qrename", NULL, cmd_qrename, "builder", TRUE);
  add_cmd("qdelete", NULL, cmd_qdelete, "builder", TRUE);
}
