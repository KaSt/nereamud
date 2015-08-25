//*****************************************************************************
//
// dialog.c
//
// This is a module that allows NPCs to carry on basic conversation with
// players. Dialogs are initiated by approaching (or greeting) a mob that has
// an attached dialog. Players are presented a list of questions they can ask
// the NPC, and the NPC will respond in fashion to each one. Questions can have
// checks to make sure a PC can ask the question, and they can also have 
// executed scripts at the end of each question.
//
// Dialogs can also be a means for interacting with NPCs in other ways. For
// instance, training skills or buying/selling items.
//
// Known bugs:
//   * if we try to end a dialog while we've pushed on another input handler
//     (e.g. while training, shopping) undefined behavior will result.
//
//*****************************************************************************

#include "../mud.h"
#include "../utils.h"
#include "../character.h"
#include "../world.h"
#include "../storage.h"
#include "../auxiliary.h"
#include "../hooks.h"
#include "../socket.h"
#include "../inform.h"
#include "dialog.h"
#include "dedit.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../scripts/scripts.h"
#include "../scripts/pychar.h"
#include "../scripts/pyplugs.h"



//*****************************************************************************
// auxiliary data
//*****************************************************************************
typedef struct {
  CHAR_DATA *talker; // who are we talking with?
  char       *panel; // which discussion panel are we on?
  char      *dialog; // the key for the dialog we use?
  LIST   *dialogers; // people reading our dialog
  LIST  *extensions; // extra questions we support
  bool        dmenu; // do we need a dialog menu?
} DIALOG_AUX_DATA;

DIALOG_AUX_DATA *newDialogAuxData(void) {
  DIALOG_AUX_DATA *data = malloc(sizeof(DIALOG_AUX_DATA));
  data->talker     = NULL;
  data->panel      = strdup("");
  data->dialog     = strdup("");
  data->dialogers  = newList();
  data->extensions = newList();
  data->dmenu      = FALSE;
  return data;
}

void deleteDialogAuxData(DIALOG_AUX_DATA *data) {
  if(data->panel)      free(data->panel);
  if(data->dialog)     free(data->dialog);
  if(data->dialogers)  free(data->dialogers);
  if(data->extensions) deleteListWith(data->extensions, deleteDialogQuestion);
  free(data);
}

void dialogAuxDataCopyTo(DIALOG_AUX_DATA *from, DIALOG_AUX_DATA *to) {
  if(to->panel)      free(to->panel);
  if(to->dialog)     free(to->dialog);
  if(to->dialogers)  deleteList(to->dialogers);
  if(to->extensions) deleteListWith(to->extensions, deleteDialogQuestion);
  to->talker     = from->talker;
  to->panel      = strdupsafe(from->panel);
  to->dialog     = strdupsafe(from->dialog);
  to->dialogers  = listCopyWith(from->dialogers, identity_func);
  to->extensions = listCopyWith(from->extensions, dialogQuestionCopy);
}

DIALOG_AUX_DATA *dialogAuxDataCopy(DIALOG_AUX_DATA *data) {
  DIALOG_AUX_DATA *newdata = newDialogAuxData();
  dialogAuxDataCopyTo(data, newdata);
  return newdata;
}

DIALOG_AUX_DATA *dialogAuxDataRead(STORAGE_SET *set) {
  DIALOG_AUX_DATA *data = newDialogAuxData();
  if(data->dialog)     free(data->dialog);
  if(data->extensions) deleteListWith(data->extensions, deleteDialogQuestion);
  data->dialog     = strdup(read_string(set, "dialog"));
  data->extensions = gen_read_list(read_list(set, "extensions"), dialogQuestionRead);

  return data;
}

STORAGE_SET *dialogAuxDataStore(DIALOG_AUX_DATA *data) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "dialog", data->dialog);
  store_list(set, "extensions", gen_store_list(data->extensions, dialogQuestionStore));
  return set;
}

void charSetDialogPanel(CHAR_DATA *ch, const char *panel) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  if(data->panel) free(data->panel);
  data->panel = strdupsafe(panel);
}

const char *charGetDialogPanel(CHAR_DATA *ch) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  return data->panel;
}

void charSetDialogTalker(CHAR_DATA *ch, CHAR_DATA *talker) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  data->talker = talker;
}

CHAR_DATA *charGetDialogTalker(CHAR_DATA *ch) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  return data->talker;
}

void charSetDialog(CHAR_DATA *ch, const char *key) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  if(data->dialog) free(data->dialog);
  data->dialog = strdupsafe(key);
}

const char *charGetDialog(CHAR_DATA *ch) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  return data->dialog;
}

LIST *charGetDialogers(CHAR_DATA *ch) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  return data->dialogers;
}

bool charNeedsDialogMenu(CHAR_DATA *ch) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  return data->dmenu;
}

void charSetNeedsDialogMenu(CHAR_DATA *ch, bool val) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  data->dmenu = val;
}

void charExtendDialog(CHAR_DATA *ch, DIALOG_QUESTION *qst) {
  DIALOG_AUX_DATA *data = charGetAuxiliaryData(ch, "dialog_data");
  listQueue(data->extensions, qst);
}



//*****************************************************************************
// local variables, datastructures, functions
//*****************************************************************************

//
// neeed by a couple functions before them
void charStartDialog(CHAR_DATA *ch, CHAR_DATA *talker);
void charEndDialog(CHAR_DATA *ch);

//
// returns whether or not the character can ask the given dialog question
bool charCanAskDialogQuestion(CHAR_DATA *ch, DIALOG_QUESTION *qst) {
  // make sure we're in the right panel
  if(!is_keyword(dialogQuestionGetPanels(qst), charGetDialogPanel(ch), FALSE))
    return FALSE;
  // make sure our check is alright
  else if(!*dialogQuestionGetCheck(qst))
    return TRUE;
  else {
    const char *locale = get_key_locale(charGetDialog(charGetDialogTalker(ch)));
    const char  *check = dialogQuestionGetCheck(qst);
    PyObject     *pych = charGetPyForm(ch);
    PyObject     *pyme = charGetPyForm(charGetDialogTalker(ch));
    PyObject     *dict = restricted_script_dict();
    PyDict_SetItemString(dict, "me", pyme);
    PyDict_SetItemString(dict, "ch", pych);
    bool          ret = TRUE;

    // evaluate the code
    PyObject *retval = eval_script(dict, check, locale);

    // did we encounter an error?
    if(retval == NULL)
      ret = FALSE;
    // append the output
    else if(PyInt_Check(retval))
      ret = (PyInt_AsLong(retval) ? TRUE : FALSE);
    else if(retval == Py_None)
      ret = FALSE;
    // invalid return type...
    else {
      log_string("dialog check had invalid evaluation: %s", check);
      ret = FALSE;
    }

    // garbage collection
    Py_XDECREF(dict);
    Py_XDECREF(pyme);
    Py_XDECREF(pych);
    return ret;
  }
}

//
// compares two dialog questions based on their query text
int dqquerycmp(DIALOG_QUESTION *dq1, DIALOG_QUESTION *dq2) {
  return strcasecmp(dialogQuestionGetQuery(dq1),
		    dialogQuestionGetQuery(dq2));
}

//
// returns a list of the dialog questions we can use at the moment. List but not
// contents must be deleted after use.
LIST *charGetDialogQuestions(CHAR_DATA *ch) {
  CHAR_DATA       *pers = charGetDialogTalker(ch);
  DIALOG_DATA   *dialog = worldGetType(gameworld, "dialog",charGetDialog(pers));
  LIST       *questions = newList();
  if(dialog != NULL) {
    LIST_ITERATOR    *q_i = newListIterator(dialogGetQuestions(dialog));
    DIALOG_QUESTION    *q = NULL;
    ITERATE_LIST(q, q_i) {
      if(charCanAskDialogQuestion(ch, q))
	listPutWith(questions, q, dqquerycmp);
    } deleteListIterator(q_i);
  }

  // find all of our extended questions
  DIALOG_AUX_DATA *pauxdialog = charGetAuxiliaryData(pers, "dialog_data");
  if(listSize(pauxdialog->extensions) > 0) {
    LIST_ITERATOR *q_i = newListIterator(pauxdialog->extensions);
    DIALOG_QUESTION *q = NULL;
    ITERATE_LIST(q, q_i) {
      if(charCanAskDialogQuestion(ch, q))
	listQueue(questions, q);
    } deleteListIterator(q_i);
  }

  // return the questions we found
  return questions;
}

//
// the function that displays a prompt to a character when in dialog mode
void show_dialog_prompt(SOCKET_DATA *sock) {
  CHAR_DATA *ch = socketGetChar(sock);
  // if our character has died, pop our input handler
  if(ch == NULL)
    socketPopInputHandler(sock);
  // we entered an actual command, and not just a newline
  else if(charNeedsDialogMenu(ch)) {
    BUFFER        *buf = newBuffer(MAX_BUFFER);
    LIST    *questions = charGetDialogQuestions(ch);
    int          count = 1;

    // if we have no questions and we're not at start, put us back at start
    if(listSize(questions) == 0 && strcasecmp("start", charGetDialogPanel(ch))){
      charSetDialogPanel(ch, "start");
      deleteList(questions);
      questions = charGetDialogQuestions(ch);
    }

    // list all of our responses
    if(listSize(questions) > 0) {
      bprintf(buf, "\r\n{wResponses:{n\r\n");
      LIST_ITERATOR *q_i = newListIterator(questions);
      DIALOG_QUESTION *q = NULL;
      ITERATE_LIST(q, q_i) {
	bprintf(buf, "{g  %2d) %s\r\n", count, dialogQuestionGetQuery(q));
	count++;
      } deleteListIterator(q_i);
    }

    // append our end option
    bprintf(buf, "{g  %2d) Goodbye\r\n", count);
    
    // send the prompt out
    message(ch, charGetDialogTalker(ch), NULL, NULL, FALSE, TO_CHAR, 
	    bufferString(buf));

    // garbage collection
    deleteList(questions);
    deleteBuffer(buf);
  }

  send_to_char(ch, "{gEnter a choice, or Q to say goodbye: ");
}

//
// a function that handles our commands in dialog mode
void dialog_input_handler(SOCKET_DATA *sock, char *input) {
  CHAR_DATA     *ch = socketGetChar(sock);
  if(ch == NULL)
    return;
  CHAR_DATA   *talker = charGetDialogTalker(ch);
  charSetNeedsDialogMenu(ch, FALSE);

  switch(toupper(*input)) {
    // we're trying to break out of the input handler
  case 'Q':
    charEndDialog(ch);
    return;
    // are we trying to do one of our responses?
  default: {
    // make sure it's a numeric choice
    if(!isdigit(*input))
      return;

    // get our options and choice
    int                choice = atoi(input) - 1;
    LIST             *options = charGetDialogQuestions(ch);
    DIALOG_QUESTION *question = listGet(options, choice);
    const char    *dialog_key = charGetDialog(talker);
    bool                 menu = TRUE;
    bool                  end = FALSE;

    // if we have a question, display the response
    if(question == NULL) {
      // are we trying to terminate the dialog?
      if(choice == listSize(options))
	end = TRUE;
      menu = FALSE;
    }
    else {
      BUFFER   *resp = bufferCopy(dialogQuestionGetResponseBuf(question));
      PyObject *pyme = charGetPyForm(talker);
      char   *locale = strdupsafe(get_key_locale(dialog_key));
      
      // expand our dynamic descriptions
      expand_dynamic_descs(resp, pyme, ch, locale);

      // format our response
      bufferFormat(resp, SCREEN_WIDTH, PARA_INDENT);

      // send the response
      if(*bufferString(resp))
	send_to_socket(sock, "%s responds:\r\n{c%s", see_char_as(ch, talker),
		       bufferString(resp));
      else
	send_to_socket(sock, "%s has no response.\r\n", see_char_as(ch,talker));

      // set our new panel
      if(*dialogQuestionGetDestPanel(question))
	charSetDialogPanel(ch, dialogQuestionGetDestPanel(question));

      // run any scripts we might have
      if(*dialogQuestionGetScript(question)) {
	PyObject     *dict = restricted_script_dict();
	PyObject     *pych = charGetPyForm(ch);
	PyDict_SetItemString(dict, "me", pyme);
	PyDict_SetItemString(dict, "ch", pych);

	// run the script
	run_script(dict, dialogQuestionGetScript(question), locale);

	// garbage collection
	Py_XDECREF(dict);
	Py_XDECREF(pych);
      }

      // do our garbage collection
      deleteBuffer(resp);
      Py_XDECREF(pyme);
      free(locale);
    }

    // set our menu status
    charSetNeedsDialogMenu(ch, menu);

    // are we trying to exit the dialog?
    if(end == TRUE)
      charEndDialog(ch);
    
    // garbage collection
    deleteList(options);
    return;
  }
  }
}

//
// stop a character conversing with another person
void charEndDialog(CHAR_DATA *ch) {
  // run the ending script, if we have one
  CHAR_DATA   *talker = charGetDialogTalker(ch);
  DIALOG_DATA *dialog = worldGetType(gameworld, "dialog",charGetDialog(talker));
  if(dialog && *dialogGetEndScript(dialog)) {
    PyObject *dict = restricted_script_dict();
    PyObject *pyme = charGetPyForm(talker);
    PyObject *pych = charGetPyForm(ch);
    PyDict_SetItemString(dict, "me", pyme);
    PyDict_SetItemString(dict, "ch", pych);

    // run the end script
    run_script(dict, dialogGetEndScript(dialog), 
	       get_key_locale(dialogGetKey(dialog)));

    // garbage collection
    Py_XDECREF(dict);
    Py_XDECREF(pyme);
    Py_XDECREF(pych);
  }

  // sever the connection between the two characters
  listRemove(charGetDialogers(talker), ch);
  charSetDialogTalker(ch, NULL);
  charSetDialogPanel(ch, NULL);
  if(charGetSocket(ch))
    socketPopInputHandler(charGetSocket(ch));
}

//
// start a character conversing with another person
void charStartDialog(CHAR_DATA *ch, CHAR_DATA *talker) {
  listQueue(charGetDialogers(talker), ch);
  charSetDialogPanel(ch, "start");
  charSetDialogTalker(ch, talker);
  charSetNeedsDialogMenu(ch, TRUE);
  if(charGetSocket(ch))
    socketPushInputHandler(charGetSocket(ch), dialog_input_handler, 
			   show_dialog_prompt, "dialog");
}

//
// stops our dialog and anyone having dialog with us
void stop_dialogs_with(CHAR_DATA *ch) {
  if(charGetDialogTalker(ch))
    charEndDialog(ch);
  LIST       *dialogers = charGetDialogers(ch);
  if(listSize(dialogers) > 0) {
    LIST_ITERATOR *pers_i = newListIterator(dialogers);
    CHAR_DATA       *pers = NULL;
    ITERATE_LIST(pers, pers_i) {
      charEndDialog(pers);
    } deleteListIterator(pers_i);
  }
}

//
// a hook that tries to stop all the dialogs with a character
void stop_dialogs_hook(const char *info) {
  CHAR_DATA *ch = NULL;
  hookParseInfo(info, &ch);
  stop_dialogs_with(ch);
}

//
// whenever we move, stop any dialog we have going on
void stop_dialogs_move_hook(const char *info) {
  CHAR_DATA   *ch = NULL;
  ROOM_DATA *room = NULL;
  EXIT_DATA *exit = NULL;
  hookParseInfo(info, &ch, &room, &exit);
  stop_dialogs_with(ch);
}

//
// same as try_start_dialog, but users can also supply a custom greeting message
void try_start_dialog_full(CHAR_DATA *ch, CHAR_DATA *other, const char *greet) {
  DIALOG_DATA  *dialog = worldGetType(gameworld, "dialog",charGetDialog(other));
  if(dialog != NULL) {
    // figure out what we'll be using as a greeting messg: custom or default
    BUFFER *gbuf = newBuffer(1);
    bufferCat(gbuf, (greet ? greet : dialogGetGreet(dialog)));

    // figure out our locale
    char *locale = strdup(get_key_locale(dialogGetKey(dialog)));

    // send out our greet message if neccessary
    PyObject *pyme = charGetPyForm(other);

    // expand any dynamic descriptions the buffer might have
    expand_dynamic_descs(gbuf, pyme, ch, locale);

    // format the buffer
    bufferFormat(gbuf, SCREEN_WIDTH, PARA_INDENT);

    // send it out
    send_to_char(ch, "%s acknowledges you%s:\r\n{c%s", 
		 see_char_as(ch, other), 
		 (*bufferString(gbuf) ? " and responds" : ""),
		 bufferString(gbuf));

    // start the dialog
    charStartDialog(ch, other);

    // garbage collection
    deleteBuffer(gbuf);
    Py_XDECREF(pyme);
    free(locale);
  }
}

//
// tries to start up a dialog between the two persons
void try_start_dialog(CHAR_DATA *ch, CHAR_DATA *other) {
  try_start_dialog_full(ch, other, NULL);
}

//
// a greet hook that tries to start a dialog between two persons
void try_start_dialog_hook(const char *info) {
  CHAR_DATA    *ch = NULL;
  CHAR_DATA *other = NULL;
  hookParseInfo(info, &ch, &other);
  try_start_dialog(ch, other);
}



//*****************************************************************************
// implementation of dialog.h
//*****************************************************************************
struct dialog_data {
  char         *key; // our unique identifier in the world database
  char        *name; // the name of our dialog for reference by builders
  BUFFER     *greet; // the message shown when the dialog first starts
  BUFFER *endscript; // a script run when the dialog is terminated
  LIST   *questions; // a list of questions that can be asked
};

struct dialog_question {
  char      *panels; // the list of panels we belong to
  char    *to_panel; // the panel we lead to
  char     *pycheck; // a python statement of whether we can ask the question
  char       *query; // the question ask
  BUFFER  *response; // what the person says in response
  BUFFER    *script; // the script that's executed after asking
};

DIALOG_DATA *newDialog(void) {
  DIALOG_DATA *data = malloc(sizeof(DIALOG_DATA));
  data->key         = strdup("");
  data->name        = strdup("");
  data->questions   = newList();
  data->endscript   = newBuffer(1);
  data->greet       = newBuffer(1);
  return data;
}

void deleteDialog(DIALOG_DATA *data) {
  if(data->key)       free(data->key);
  if(data->name)      free(data->name);
  if(data->questions) deleteListWith(data->questions, deleteDialogQuestion);
  if(data->greet)     deleteBuffer(data->greet);
  if(data->endscript) deleteBuffer(data->endscript);
  free(data);
}

void dialogCopyTo(DIALOG_DATA *from, DIALOG_DATA *to) {
  if(to->key)       free(to->key);
  if(to->name)      free(to->name);
  if(to->questions) deleteListWith(to->questions, deleteDialogQuestion);
  to->key       = strdupsafe(from->key);
  to->name      = strdupsafe(from->name);
  to->questions = listCopyWith(from->questions, dialogQuestionCopy);
  bufferCopyTo(from->greet, to->greet);
  bufferCopyTo(from->endscript, to->endscript);
}

DIALOG_DATA *dialogCopy(DIALOG_DATA *data) {
  DIALOG_DATA *newdata = newDialog();
  dialogCopyTo(data, newdata);
  return newdata;
}

STORAGE_SET *dialogStore(DIALOG_DATA *data) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "name",  data->name);
  store_string(set, "greet", bufferString(data->greet));
  store_string(set, "endscript", bufferString(data->endscript));
  store_list(set,   "questions", gen_store_list(data->questions, 
						dialogQuestionStore));
  return set;
}

DIALOG_DATA *dialogRead(STORAGE_SET *set) {
  DIALOG_DATA *data = newDialog();
  dialogSetName(data, read_string(set, "name"));
  dialogSetGreet(data, read_string(set, "greet"));
  dialogSetEndScript(data, read_string(set, "endscript"));
  
  LIST *qstns = gen_read_list(read_list(set, "questions"), dialogQuestionRead);
  DIALOG_QUESTION *q = NULL;
  while( (q = listPop(qstns)) != NULL)
    dialogAddQuestion(data, q);
  deleteList(qstns);
  return data;
}

void dialogSetKey(DIALOG_DATA *dialog, const char *key) {
  if(dialog->key) free(dialog->key);
  dialog->key = strdupsafe(key);
}

const char *dialogGetKey(DIALOG_DATA *dialog) {
  return dialog->key;
}

const char *dialogGetName(DIALOG_DATA *dialog) {
  return dialog->name;
}

void dialogSetName(DIALOG_DATA *dialog, const char *name) {
  if(dialog->name) free(dialog->name);
  dialog->name = strdupsafe(name);
}

void dialogSetEndScript(DIALOG_DATA *dialog, const char *script) {
  bufferClear(dialog->endscript);
  bufferCat(dialog->endscript, script);
}

void dialogSetGreet(DIALOG_DATA *dialog, const char *greet) {
  bufferClear(dialog->greet);
  bufferCat(dialog->greet, greet);
}

const char *dialogGetEndScript(DIALOG_DATA *dialog) {
  return bufferString(dialog->endscript);
}

BUFFER *dialogGetEndScriptBuf(DIALOG_DATA *dialog) {
  return dialog->endscript;
}

const char *dialogGetGreet(DIALOG_DATA *dialog) {
  return bufferString(dialog->greet);
}

BUFFER *dialogGetGreetBuf(DIALOG_DATA *dialog) {
  return dialog->greet;
}

LIST *dialogGetQuestions(DIALOG_DATA *dialog) {
  return dialog->questions;
}

void dialogAddQuestion(DIALOG_DATA *dialog, DIALOG_QUESTION *question) {
  listQueue(dialog->questions, question);
}

DIALOG_QUESTION *newDialogQuestion(void) {
  DIALOG_QUESTION *data = malloc(sizeof(DIALOG_QUESTION));
  data->panels   = strdup("");
  data->to_panel = strdup("");
  data->query    = strdup("");
  data->pycheck  = strdup("");
  data->response = newBuffer(1);
  data->script   = newBuffer(1);
  return data;
}

void deleteDialogQuestion(DIALOG_QUESTION *qst) {
  if(qst->panels)   free(qst->panels);
  if(qst->to_panel) free(qst->to_panel);
  if(qst->query)    free(qst->query);
  if(qst->pycheck)  free(qst->pycheck);
  if(qst->response) deleteBuffer(qst->response);
  if(qst->script)   deleteBuffer(qst->script);
  free(qst);
}

void dialogQuestionCopyTo(DIALOG_QUESTION *from, DIALOG_QUESTION *to) {
  if(to->panels)   free(to->panels);
  if(to->to_panel) free(to->to_panel);
  if(to->query)    free(to->query);
  if(to->pycheck)  free(to->pycheck);
  bufferClear(to->response);
  bufferClear(to->script);

  to->panels   = strdupsafe(from->panels);
  to->to_panel = strdupsafe(from->to_panel);
  to->query    = strdupsafe(from->query);
  to->pycheck  = strdupsafe(from->pycheck);
  bufferCat(to->response, bufferString(from->response));
  bufferCat(to->script, bufferString(from->script));
}

DIALOG_QUESTION *dialogQuestionCopy(DIALOG_QUESTION *qst) {
  DIALOG_QUESTION *newqst = newDialogQuestion();
  dialogQuestionCopyTo(qst, newqst);
  return newqst;
}

STORAGE_SET *dialogQuestionStore(DIALOG_QUESTION *qst) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "panels",   qst->panels);
  store_string(set, "to_panel", qst->to_panel);
  store_string(set, "query",    qst->query);
  store_string(set, "pycheck",  qst->pycheck);
  store_string(set, "response", bufferString(qst->response));
  store_string(set, "script",   bufferString(qst->script));
  return set;
}

DIALOG_QUESTION *dialogQuestionRead(STORAGE_SET *set) {
  DIALOG_QUESTION *data = newDialogQuestion();
  dialogQuestionSetPanels(data, read_string(set, "panels"));
  dialogQuestionSetDestPanel(data, read_string(set, "to_panel"));
  dialogQuestionSetQuery(data, read_string(set, "query"));
  dialogQuestionSetCheck(data, read_string(set, "pycheck"));
  dialogQuestionSetResponse(data, read_string(set, "response"));
  dialogQuestionSetScript(data, read_string(set, "script"));
  return data;
}

void dialogQuestionSetQuery(DIALOG_QUESTION *qst, const char *query) {
  if(qst->query) free(qst->query);
  qst->query = strdupsafe(query);
}

void dialogQuestionSetResponse(DIALOG_QUESTION *qst, const char *response) {
  bufferClear(qst->response);
  bufferCat(qst->response, response);
}

void dialogQuestionSetCheck(DIALOG_QUESTION *qst,const char *pycheck) {
  if(qst->pycheck) free(qst->pycheck);
  qst->pycheck = strdupsafe(pycheck);
}

void dialogQuestionSetScript(DIALOG_QUESTION *qst, const char *script) {
  bufferClear(qst->script);
  bufferCat(qst->script, script);
}

void dialogQuestionSetPanels(DIALOG_QUESTION *qst, const char *panels) {
  if(qst->panels) free(qst->panels);
  qst->panels = strdupsafe(panels);
}

void dialogQuestionSetDestPanel(DIALOG_QUESTION *qst, const char *panel) {
  if(qst->to_panel) free(qst->to_panel);
  qst->to_panel = strdup(panel);
}

const char *dialogQuestionGetQuery(DIALOG_QUESTION *qst) {
  return qst->query;
}

const char *dialogQuestionGetResponse(DIALOG_QUESTION *qst) {
  return bufferString(qst->response);
}

BUFFER *dialogQuestionGetResponseBuf(DIALOG_QUESTION *qst) {
  return qst->response;
}

const char *dialogQuestionGetCheck(DIALOG_QUESTION *qst) {
  return qst->pycheck;
}

const char *dialogQuestionGetScript(DIALOG_QUESTION *qst) {
  return bufferString(qst->script);
}

BUFFER *dialogQuestionGetScriptBuf(DIALOG_QUESTION *qst) {
  return qst->script;
}

const char *dialogQuestionGetPanels(DIALOG_QUESTION *qst) {
  return qst->panels;
}

const char *dialogQuestionGetDestPanel(DIALOG_QUESTION *qst) {
  return qst->to_panel;
}



//*****************************************************************************
// Python extensions
//*****************************************************************************
PyObject *PyChar_GetDialog(PyObject *self, void *closure) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) return NULL;
  else           return Py_BuildValue("s", charGetDialog(ch));
}

int PyChar_SetDialog(PyObject *self, PyObject *arg, void *closure) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character uid %d does not exist.",
		 PyChar_AsUid(self));
    return -1;
  }

  if(arg == Py_None) {
    charSetDialog(ch, NULL);
    return 0;
  }
  else if(PyString_Check(arg)) {
    const char     *key = get_fullkey_relative(PyString_AsString(arg), 
					       get_script_locale());
    DIALOG_DATA *dialog = worldGetType(gameworld, "dialog", key);
    if(dialog == NULL) {
      PyErr_Format(PyExc_StandardError, "dialog, %s, does not exist!\r\n", key);
      return -1;
    }
    else {
      charSetDialog(ch, key); 
      return 0;
    }
  }
  else {
    PyErr_Format(PyExc_TypeError, "Character dialog must be a string key");
    return -1;
  }
}

//
// if a character is in dialog, terminates the dialog
PyObject *PyChar_EndDialog(PyObject *self, void *closure) {
  // get our character represenetation
  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch != NULL) {
    if(charGetDialogTalker(ch) != NULL)
      charEndDialog(ch);
    return Py_BuildValue("i", 1);
  }
  else {
    PyErr_Format(PyExc_StandardError, "tried to end dialog for nonexistant "
		 "Char, %d", PyChar_AsUid(self));
    return NULL;
  }
}

//
// starts a dialog with someone (the person with the dialog on them). Sends an
// optional start message to the person. If the optional message is not
// provided, then the dialoger's default greet message is displayed.
PyObject *PyChar_TryDialog(PyObject *self, PyObject *args, void *closure) {
  // try to parse our arguments
  PyObject *pydialoger = NULL;
  char           *mssg = NULL;
  CHAR_DATA  *dialoger = NULL;
  if(!PyArg_ParseTuple(args, "O|s", &pydialoger, &mssg)) {
    PyErr_Format(PyExc_TypeError,"Improper arguments supplied to dialog_start");
    return NULL;
  }

  // parse out our character
  CHAR_DATA *ch = PyChar_AsChar(self);
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "Character uid %d does not exist.",
		 PyChar_AsUid(self));
    return NULL;
  }

  // parse out our dialoger
  if(PyChar_Check(pydialoger)) {
    dialoger = PyChar_AsChar(pydialoger);
    if(dialoger == NULL) {
      PyErr_Format(PyExc_StandardError, "Dialog char uid %d does not exist.",
		   PyChar_AsUid(pydialoger));
      return NULL;
    }
  }
  else {
    PyErr_Format(PyExc_TypeError, "The first arg supplied to dialog must be a "
		 "character with a dialog!");
    return NULL;
  }

  // try doing the dialog
  try_start_dialog_full(ch, dialoger, mssg);
  return Py_BuildValue("i", 1);
}

//
// extends the character's dialog with a new question specific to the character
PyObject *PyChar_ExtendDialog(PyObject *self, PyObject *args) {
  CHAR_DATA  *ch = PyChar_AsChar(self);
  char    *query = NULL;
  char    *panel = NULL;
  char *to_panel = NULL;
  char    *check = NULL;
  char *response = NULL;
  char   *script = NULL;

  if(!PyArg_ParseTuple(args, "s|sssss", &query, &panel, &to_panel, &check,
		       &response, &script)) {
    PyErr_Format(PyExc_TypeError,"Improper args supplied to extend_dialog.");
    return NULL;
  }

  // make sure we exist
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, "character to extend does not exist.");
    return NULL;
  }

  // make the dialog question
  DIALOG_AUX_DATA      *aux = charGetAuxiliaryData(ch, "dialog_data");
  DIALOG_QUESTION *question = newDialogQuestion();
  dialogQuestionSetQuery(question, query);
  dialogQuestionSetPanels(question, (panel ? panel : "start"));
  dialogQuestionSetDestPanel(question, (to_panel ? to_panel : ""));
  dialogQuestionSetCheck(question, (check ? check : ""));
  dialogQuestionSetResponse(question, (response ? response : ""));
  dialogQuestionSetScript(question, (script ? script : ""));
  listQueue(aux->extensions, question);
  return Py_BuildValue("i", 1);
}



//*****************************************************************************
// initialization
//*****************************************************************************
void init_dialogs(void) {
  // set up our olc
  init_dedit();

  // set up our auxiliary data
  auxiliariesInstall("dialog_data",
		     newAuxiliaryFuncs(AUXILIARY_TYPE_CHAR,
				       newDialogAuxData,    deleteDialogAuxData,
				       dialogAuxDataCopyTo, dialogAuxDataCopy,
				       dialogAuxDataStore,  dialogAuxDataRead));

  // add our new world type
  worldAddType(gameworld, "dialog", dialogRead, dialogStore, deleteDialog, 
  	       dialogSetKey);

  // set up our hooks
  hookAdd("char_from_game", stop_dialogs_hook);
  hookAdd("exit",           stop_dialogs_move_hook);
  hookAdd("greet",          try_start_dialog_hook);

  // set up our Python extensions
  PyChar_addGetSetter("dialog",       PyChar_GetDialog, PyChar_SetDialog, NULL);
  PyChar_addMethod   ("dialog_start", PyChar_TryDialog, METH_VARARGS,     NULL);
  PyChar_addMethod   ("dialog_end",   PyChar_EndDialog, METH_NOARGS,      NULL);
  PyChar_addMethod   ("dialog_extend",PyChar_ExtendDialog, METH_VARARGS,  NULL);
}
