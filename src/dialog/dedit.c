//*****************************************************************************
//
// dedit.c
//
// functions for allowing players to edit dialogs in OLC.
//
//*****************************************************************************

#include "../mud.h"
#include "../utils.h"
#include "../zone.h"
#include "../world.h"
#include "../socket.h"
#include "../character.h"
#include "../room.h"
#include "dialog.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../olc2/olc.h"
#include "../editor/editor.h"
#include "../scripts/scripts.h"
#include "../scripts/script_editor.h"



//*****************************************************************************
// dialog question olc functions
//*****************************************************************************
#define DQEDIT_QUERY     1
#define DQEDIT_CHECK     2
#define DQEDIT_PANELS    3
#define DQEDIT_TO_PANEL  4

void dqedit_menu(SOCKET_DATA *sock, DIALOG_QUESTION *data) {
  send_to_socket(sock,
		 "{g1) Question message : {c%s\r\n"
		 "{g2) Panels           : {c%s\r\n"
		 "{g3) Destination panel: {c%s\r\n"
		 "{g4) Python check     : ",
		 dialogQuestionGetQuery(data),
		 dialogQuestionGetPanels(data),
		 dialogQuestionGetDestPanel(data));
  script_display(sock, dialogQuestionGetCheck(data), FALSE);
  send_to_socket(sock, 
		 "%s"
		 "{g5) Response\r\n"
		 "{c%s\r\n"
		 "{g6) Script\r\n",
		 (*dialogQuestionGetCheck(data) ? "" : "\r\n"),
		 dialogQuestionGetResponse(data));
  script_display(sock, dialogQuestionGetScript(data), FALSE);
}

int dqedit_chooser(SOCKET_DATA *sock, DIALOG_QUESTION *data,const char *option){
  switch(toupper(*option)) {
  case '1':
    send_to_socket(sock, "Enter a new question to ask: ");
    return DQEDIT_QUERY;
  case '2':
    send_to_socket(sock, "Enter the panels this question belongs to: ");
    return DQEDIT_PANELS;
  case '3':
    send_to_socket(sock, "Enter the panel this question leads to: ");
    return DQEDIT_TO_PANEL;
  case '4':
    send_to_socket(sock, "Enter check to ensure char can ask the question: ");
    return DQEDIT_CHECK;
  case '5':
    socketStartEditor(sock, text_editor, dialogQuestionGetResponseBuf(data));
    return MENU_NOCHOICE;
  case '6':
    socketStartEditor(sock, script_editor, dialogQuestionGetScriptBuf(data));
    return MENU_NOCHOICE;
  default:
    return MENU_CHOICE_INVALID;
  }
}

bool dqedit_parser(SOCKET_DATA *sock, DIALOG_QUESTION *data, int choice, 
		  const char *arg) {
  switch(choice) {
  case DQEDIT_QUERY:
    dialogQuestionSetQuery(data, arg);
    return TRUE;
  case DQEDIT_PANELS:
    dialogQuestionSetPanels(data, arg);
    return TRUE;
  case DQEDIT_TO_PANEL:
    dialogQuestionSetDestPanel(data, arg);
    return TRUE;
  case DQEDIT_CHECK:
    dialogQuestionSetCheck(data, arg);
    return TRUE;
  default:
    return FALSE;
  }
}



//*****************************************************************************
// dialog olc functions
//*****************************************************************************
#define DEDIT_NAME            1
#define DEDIT_EDIT_QUESTION   2
#define DEDIT_DELETE_QUESTION 3



void dedit_menu(SOCKET_DATA *sock, DIALOG_DATA *data) {
  send_to_socket(sock,
		 "{y[{c%s{y]\r\n"
		 "{g1) Name\r\n"
		 "{c%s\r\n"
		 "{g2) Greeting\r\n"
		 "{c%s\r\n"
		 "{g3) Ending script\r\n",
		 dialogGetKey(data), dialogGetName(data), dialogGetGreet(data));
  script_display(sock, dialogGetEndScript(data), FALSE);
  send_to_socket(sock, "\r\n{wResponses:{n\r\n");

  LIST_ITERATOR *q_i = newListIterator(dialogGetQuestions(data));
  DIALOG_QUESTION *q = NULL;
  int          count = 0;
  ITERATE_LIST(q, q_i) {
    send_to_socket(sock, 
		   "  {g%2d) {c%s\r\n"
		   "      {c%s {y[{cleads to %s{y]\r\n", count,
		   dialogQuestionGetQuery(q),
		   dialogQuestionGetPanels(q), 
		   (*dialogQuestionGetDestPanel(q) ? 
		    dialogQuestionGetDestPanel(q) : "the same panel"));
    count++;
  } deleteListIterator(q_i);

  send_to_socket(sock,
		 "  {gE) Edit question\r\n"
		 "  N) New question\r\n"
		 "  D) Delete question\r\n");
}

int dedit_chooser(SOCKET_DATA *sock, DIALOG_DATA *data, const char *option) {
  switch(toupper(*option)) {
  case '1':
    send_to_socket(sock, "Enter a new name: ");
    return DEDIT_NAME;
  case '2':
    socketStartEditor(sock, text_editor, dialogGetGreetBuf(data));
    return MENU_NOCHOICE;
  case '3':
    socketStartEditor(sock, script_editor, dialogGetEndScriptBuf(data));
    return MENU_NOCHOICE;
  case 'E':
    if(listSize(dialogGetQuestions(data)) == 0)
      return MENU_CHOICE_INVALID;
    send_to_socket(sock, "Enter question number to edit: ");
    return DEDIT_EDIT_QUESTION;
  case 'N': {
    DIALOG_QUESTION *qst = newDialogQuestion();
    dialogAddQuestion(data, qst);
    do_olc(sock, dqedit_menu, dqedit_chooser, dqedit_parser,
	   NULL, NULL, NULL, NULL, qst);
    return MENU_NOCHOICE;
  }
  case 'D':
    if(listSize(dialogGetQuestions(data)) == 0)
      return MENU_CHOICE_INVALID;
    send_to_socket(sock, "Enter question number to delete: ");
    return DEDIT_DELETE_QUESTION;
  default:
    return MENU_CHOICE_INVALID;
  }
}

bool dedit_parser(SOCKET_DATA *sock, DIALOG_DATA *data, int choice, 
		  const char *arg) {
  switch(choice) {
  case DEDIT_NAME:
    dialogSetName(data, arg);
    return TRUE;
  case DEDIT_EDIT_QUESTION:
    if(!isdigit(*arg))
      return FALSE;
    else {
      int val = atoi(arg);
      if(val < 0)
	return TRUE;
      else if(listSize(dialogGetQuestions(data)) <= val)
	return FALSE;
      else {
	do_olc(sock, dqedit_menu, dqedit_chooser, dqedit_parser,
	       NULL, NULL, NULL, NULL, listGet(dialogGetQuestions(data), val));
	return TRUE;
      }
    }
  case DEDIT_DELETE_QUESTION:
    if(!*arg)
      return TRUE;
    else {
      DIALOG_QUESTION *qst = listRemoveNum(dialogGetQuestions(data), atoi(arg));
      if(qst != NULL)
	deleteDialogQuestion(qst);
      return TRUE;
    }
  default:
    return FALSE;
  }
}



//*****************************************************************************
// player commands
//*****************************************************************************
void olc_save_dialog(DIALOG_DATA *dialog) {
  worldSaveType(gameworld, "dialog", dialogGetKey(dialog));
}

COMMAND(cmd_dedit) {
  ZONE_DATA     *zone = NULL;
  DIALOG_DATA *dialog = NULL;

  // we need a key
  if(!arg || !*arg)
    send_to_char(ch, "What is the name of the dialog you want to edit?\r\n");
  else {
    char locale[SMALL_BUFFER];
    char   name[SMALL_BUFFER];
    if(!parse_worldkey_relative(ch, arg, name, locale))
      send_to_char(ch, "Which dialog are you trying to edit?\r\n");
    // make sure we can edit the zone
    else if((zone = worldGetZone(gameworld, locale)) == NULL)
      send_to_char(ch, "No such zone exists.\r\n");
    else if(!canEditZone(zone, ch))
      send_to_char(ch, "You are not authorized to edit that zone.\r\n");
    else {
      // try to pull up the prototype
      dialog = worldGetType(gameworld, "dialog", get_fullkey(name, locale));
      if(dialog == NULL) {
	dialog = newDialog();
	dialogSetName(dialog, "An unfinished dialog");
	dialogSetGreet(dialog, 
		      "Hello, $n. What I have to say is not yet finished.\r\n");
	worldPutType(gameworld, "dialog", get_fullkey(name, locale), dialog);
      }

      do_olc(charGetSocket(ch), dedit_menu, dedit_chooser, dedit_parser,
 	     dialogCopy, dialogCopyTo, deleteDialog, olc_save_dialog, dialog);
    }
  }
}

// this is used for the header when printing out zone proto info
#define DIALOG_LIST_HEADER \
"Name                                                  "

const char *dlist_list_name(DIALOG_DATA *dialog) {
  static char buf[SMALL_BUFFER];
  sprintf(buf, "%-54s", dialogGetName(dialog));
  return buf;
}

COMMAND(cmd_dlist) {
  do_list(ch, (arg&&*arg?arg:get_key_locale(roomGetClass(charGetRoom(ch)))),
	  "dialog", DIALOG_LIST_HEADER, dlist_list_name);
}

COMMAND(cmd_drename) {
  char *from = NULL, *to = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word word", &from, &to))
    return;
  do_rename(ch, "dialog", from, to);
}

COMMAND(cmd_ddelete) {
  char *name = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word", &name))
    return;
  do_delete(ch, "dialog", deleteDialog, name);
}



//*****************************************************************************
// initialization
//*****************************************************************************
void init_dedit(void) {
  add_cmd("dedit",   NULL, cmd_dedit,   "builder", TRUE);
  add_cmd("dlist",   NULL, cmd_dlist,   "builder", TRUE);
  add_cmd("drename", NULL, cmd_drename, "builder", TRUE);
  add_cmd("ddelete", NULL, cmd_ddelete, "builder", TRUE);
}
