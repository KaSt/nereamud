#ifndef QUEST_H
#define QUEST_H
//*****************************************************************************
//
// quest.h
//
// This is a framework for setting up player quests. Quests are stored in the
// world database, like rooms, mobiles, objects, etc. Quests can have multiple
// stages. Each stage of a quest can also have multiple and varied objectives.
// Some common ones would be kill objectives (e.g. kill 20 orcs) and give 
// objectives (e.g. give 2 apples to your grandma). Quests must be started 
// before their progress can be tracked. When the objectives of a quest stage
// are completed, the stage is automatically completed. If the stage is the
// last stage of the quest, the entire quest is completed.
//
//*****************************************************************************

typedef struct quest_data      QUEST_DATA;
typedef struct quest_stage     QUEST_STAGE;
typedef struct quest_objective QUEST_OBJECTIVE;

//
// set up quests for use
void init_quests(void);

//
// functions for interacting with quests
QUEST_DATA            *newQuest(void);
void                deleteQuest(QUEST_DATA *quest);
void                questCopyTo(QUEST_DATA *from, QUEST_DATA *to);
QUEST_DATA           *questCopy(QUEST_DATA *quest);
STORAGE_SET         *questStore(QUEST_DATA *quest);
QUEST_DATA           *questRead(STORAGE_SET *set);
void                questSetKey(QUEST_DATA *quest, const char *key);
void               questSetName(QUEST_DATA *quest, const char *name);
void               questSetDesc(QUEST_DATA *quest, const char *desc);
const char         *questGetKey(QUEST_DATA *quest);
const char        *questGetName(QUEST_DATA *quest);
const char        *questGetDesc(QUEST_DATA *quest);
BUFFER         *questGetDescBuf(QUEST_DATA *quest);
void              questAddStage(QUEST_DATA *quest, QUEST_STAGE *stage);
void           questRemoveStage(QUEST_DATA *quest, QUEST_STAGE *stage);
QUEST_STAGE*questRemoveStageNum(QUEST_DATA *quest, int num);
LIST            *questGetStages(QUEST_DATA *quest);

//
// functions for interacting with quest stages
QUEST_STAGE         *newQuestStage(void);
void              deleteQuestStage(QUEST_STAGE *stage);
void              questStageCopyTo(QUEST_STAGE *from, QUEST_STAGE *to);
QUEST_STAGE        *questStageCopy(QUEST_STAGE *stage);
STORAGE_SET       *questStageStore(QUEST_STAGE *stage);
QUEST_STAGE        *questStageRead(STORAGE_SET *set);
void             questStageSetName(QUEST_STAGE *stage, const char *name);
void        questStageSetEndScript(QUEST_STAGE *stage, const char *script);
const char      *questStageGetName(QUEST_STAGE *stage);
const char *questStageGetEndScript(QUEST_STAGE *stage);
BUFFER  *questStageGetEndScriptBuf(QUEST_STAGE *stage);
void        questStageAddObjective(QUEST_STAGE *stage, QUEST_OBJECTIVE *ob);
void     questStageRemoveObjective(QUEST_STAGE *stage, QUEST_OBJECTIVE *ob);
QUEST_OBJECTIVE *questStageRemoveObjectiveNum(QUEST_STAGE *stage, int num);
LIST      *questStageGetObjectives(QUEST_STAGE *stage);
QUEST_DATA     *questStageGetQuest(QUEST_STAGE *stage);

//
// functions for interacting with quest objectives
QUEST_OBJECTIVE  *newQuestObjective(void);
void           deleteQuestObjective(QUEST_OBJECTIVE *ob);
void           questObjectiveCopyTo(QUEST_OBJECTIVE *from, QUEST_OBJECTIVE *to);
QUEST_OBJECTIVE *questObjectiveCopy(QUEST_OBJECTIVE *ob);
STORAGE_SET    *questObjectiveStore(QUEST_OBJECTIVE *ob);
QUEST_OBJECTIVE *questObjectiveRead(STORAGE_SET    *set);
const char   *questObjectiveGetType(QUEST_OBJECTIVE *ob);
const char   *questObjectiveGetDesc(QUEST_OBJECTIVE *ob);
HASHTABLE    *questObjectiveGetVars(QUEST_OBJECTIVE *ob);
const char    *questObjectiveGetVar(QUEST_OBJECTIVE *ob, const char *var);
int         questObjectiveGetVarInt(QUEST_OBJECTIVE *ob, const char *var);
void          questObjectiveSetType(QUEST_OBJECTIVE *ob, const char *type);
void          questObjectiveSetDesc(QUEST_OBJECTIVE *ob, const char *desc);
void           questObjectiveSetVar(QUEST_OBJECTIVE *ob, const char *var,
				    const char *val);
void        questObjectiveSetVarInt(QUEST_OBJECTIVE *ob, const char *var,
				    int val);
void        questObjectiveDeleteVar(QUEST_OBJECTIVE *ob, const char *var);
void        questObjectiveClearVars(QUEST_OBJECTIVE *ob);
QUEST_STAGE *questObjectiveGetStage(QUEST_OBJECTIVE *ob);

//
// functions for letting characters interact with quests
void     charStartQuest(CHAR_DATA *ch, QUEST_DATA *quest);
void    charCancelQuest(CHAR_DATA *ch, QUEST_DATA *quest);
void   charAdvanceQuest(CHAR_DATA *ch, QUEST_DATA *quest);
void      charFailQuest(CHAR_DATA *ch, QUEST_DATA *quest);
bool charCompletedQuest(CHAR_DATA *ch, QUEST_DATA *quest);
bool        charOnQuest(CHAR_DATA *ch, QUEST_DATA *quest);
bool    charFailedQuest(CHAR_DATA *ch, QUEST_DATA *quest);
int   charGetQuestStage(CHAR_DATA *ch, QUEST_DATA *quest);


#endif // QUEST_H
