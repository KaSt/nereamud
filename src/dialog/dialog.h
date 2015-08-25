#ifndef DIALOG_H
#define DIALOG_H
//*****************************************************************************
//
// dialog.h
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
//*****************************************************************************

typedef struct dialog_data     DIALOG_DATA;
typedef struct dialog_question DIALOG_QUESTION;

//
// prepare dialogs for use
void init_dialogs(void);

//
// functions for interacting with dialogs
DIALOG_DATA         *newDialog(void);
void              deleteDialog(DIALOG_DATA *dialog);
void              dialogCopyTo(DIALOG_DATA *from, DIALOG_DATA *to);
DIALOG_DATA        *dialogCopy(DIALOG_DATA *dialog);
STORAGE_SET       *dialogStore(DIALOG_DATA *dialog);
DIALOG_DATA        *dialogRead(STORAGE_SET *set);
void              dialogSetKey(DIALOG_DATA *dialog, const char *key);
void            dialogSetGreet(DIALOG_DATA *dialog, const char *greet);
void        dialogSetEndScript(DIALOG_DATA *dialog, const char *script);
void             dialogSetName(DIALOG_DATA *dialog, const char *name);
const char       *dialogGetKey(DIALOG_DATA *dialog);
const char      *dialogGetName(DIALOG_DATA *dialog);
const char *dialogGetEndScript(DIALOG_DATA *dialog);
BUFFER  *dialogGetEndScriptBuf(DIALOG_DATA *dialog);
const char     *dialogGetGreet(DIALOG_DATA *dialog);
BUFFER      *dialogGetGreetBuf(DIALOG_DATA *dialog);
LIST       *dialogGetQuestions(DIALOG_DATA *dialog);
void         dialogAddQuestion(DIALOG_DATA *dialog, DIALOG_QUESTION *question);

//
// functions for interacting with dialog questions
DIALOG_QUESTION    *newDialogQuestion(void);
void             deleteDialogQuestion(DIALOG_QUESTION *qst);
void             dialogQuestionCopyTo(DIALOG_QUESTION *from,DIALOG_QUESTION*to);
DIALOG_QUESTION   *dialogQuestionCopy(DIALOG_QUESTION *qst);
STORAGE_SET      *dialogQuestionStore(DIALOG_QUESTION *qst);
DIALOG_QUESTION   *dialogQuestionRead(STORAGE_SET     *set);
void           dialogQuestionSetQuery(DIALOG_QUESTION *qst, const char *query);
void        dialogQuestionSetResponse(DIALOG_QUESTION *qst,const char*response);
void           dialogQuestionSetCheck(DIALOG_QUESTION *qst,const char *pycheck);
void          dialogQuestionSetScript(DIALOG_QUESTION *qst, const char *script);
void          dialogQuestionSetPanels(DIALOG_QUESTION *qst, const char *panels);
void       dialogQuestionSetDestPanel(DIALOG_QUESTION *qst, const char *panel);
const char    *dialogQuestionGetQuery(DIALOG_QUESTION *qst);
const char *dialogQuestionGetResponse(DIALOG_QUESTION *qst);
BUFFER  *dialogQuestionGetResponseBuf(DIALOG_QUESTION *qst);
const char    *dialogQuestionGetCheck(DIALOG_QUESTION *qst);
const char   *dialogQuestionGetScript(DIALOG_QUESTION *qst);
BUFFER    *dialogQuestionGetScriptBuf(DIALOG_QUESTION *qst);
const char   *dialogQuestionGetPanels(DIALOG_QUESTION *qst);
const char*dialogQuestionGetDestPanel(DIALOG_QUESTION *qst);

//
// set or get the dialog we use when approached
void        charSetDialog(CHAR_DATA *ch, const char *key);
const char *charGetDialog(CHAR_DATA *ch);

//
// Extend the dialog system for a specific NPC. Extended questions are added
// after a character's normal dialog questions.
void charExtendDialog(CHAR_DATA *ch, DIALOG_QUESTION *qst);

#endif // DIALOG_H
