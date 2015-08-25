//*****************************************************************************
//
// quest.c
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
#include "../mud.h"
#include "../utils.h"
#include "../hooks.h"
#include "../storage.h"
#include "../world.h"
#include "../zone.h"
#include "../room.h"
#include "../auxiliary.h"
#include "../character.h"
#include "../object.h"
#include "qedit.h"
#include "quest.h"



//*****************************************************************************
// mandatory modules
//*****************************************************************************
#include "../scripts/scripts.h"
#include "../scripts/pychar.h"



//*****************************************************************************
// auxiliary data
//*****************************************************************************

//
// holds info for char's progress on one quest
typedef struct {
  int       stage; // what stage of the quest are we currently on?
  bool     failed; // have we failed the quest?
  HASHTABLE *vars; // tracks vals for things we've accomplished
} QUEST_PROGRESS;

QUEST_PROGRESS *newQuestProgress(void) {
  QUEST_PROGRESS *prog = malloc(sizeof(QUEST_PROGRESS));
  prog->stage          = 0;
  prog->failed         = FALSE;
  prog->vars           = newHashtable();
  return prog;
}

void deleteQuestProgress(QUEST_PROGRESS *prog) {
  if(hashSize(prog->vars) > 0) {
    HASH_ITERATOR *var_i = newHashIterator(prog->vars);
    const char      *key = NULL;
    char            *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      free(val);
    } deleteHashIterator(var_i);
  }
  deleteHashtable(prog->vars);
  free(prog);
}

void questProgressCopyTo(QUEST_PROGRESS *from, QUEST_PROGRESS *to) {
  // empty our current var table, if neccessary
  if(hashSize(to->vars) > 0) {
    HASH_ITERATOR *var_i = newHashIterator(to->vars);
    const char      *key = NULL;
    char            *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      free(hashRemove(to->vars, key));
    } deleteHashIterator(var_i);
  }

  // copy over our current contents, if neccessary
  if(hashSize(from->vars) > 0) {
    HASH_ITERATOR *var_i = newHashIterator(from->vars);
    const char      *key = NULL;
    char            *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      hashPut(to->vars, key, strdup(val));
    } deleteHashIterator(var_i);
  }

  to->stage  = from->stage;
  to->failed = from->failed;
}

QUEST_PROGRESS *questProgressCopy(QUEST_PROGRESS *prog) {
  QUEST_PROGRESS *newprog = newQuestProgress();
  questProgressCopyTo(prog, newprog);
  return newprog;
}

STORAGE_SET *questProgressStore(QUEST_PROGRESS *prog) {
  STORAGE_SET *set = new_storage_set();
  store_int(set,  "stage",  prog->stage);
  store_bool(set, "failed", prog->failed);
  if(hashSize(prog->vars) > 0) {
    STORAGE_SET_LIST *list = new_storage_list();
    HASH_ITERATOR   *var_i = newHashIterator(prog->vars);
    const char        *key = NULL;
    char              *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      STORAGE_SET *one_var = new_storage_set();
      store_string(one_var, "key", key);
      store_string(one_var, "val", val);
      storage_list_put(list, one_var);
    } deleteHashIterator(var_i);
    store_list(set, "vars", list);
  }
  return set;
}

QUEST_PROGRESS *questProgressRead(STORAGE_SET *set) {
  QUEST_PROGRESS   *prog = newQuestProgress();
  STORAGE_SET_LIST *vars = read_list(set, "vars");
  STORAGE_SET   *one_var = NULL;
  prog->stage            = read_int(set, "stage");
  prog->failed           = read_bool(set, "failed");

  // read in all of our values
  while( (one_var = storage_list_next(vars)) != NULL) {
    hashPut(prog->vars, read_string(one_var, "key"), 
	    strdup(read_string(one_var, "val")));
  }

  return prog;
}

const char *questProgressGetVar(QUEST_PROGRESS *prog, const char *var) {
  char *val = hashGet(prog->vars, var);
  return (val ? val : "");
}

int questProgressGetVarInt(QUEST_PROGRESS *prog, const char *var) {
  return atoi(questProgressGetVar(prog, var));
}

void questProgressSetVar(QUEST_PROGRESS *prog, const char *var,const char *val){
  if(hashIn(prog->vars, var))
    free(hashRemove(prog->vars, var));
  hashPut(prog->vars, var, strdupsafe(val));
}

void questProgressSetVarInt(QUEST_PROGRESS *prog, const char *var, int val) {
  char sval[20];
  sprintf(sval, "%d", val);
  questProgressSetVar(prog, var, sval);
}



//
// holds info for char's progress on all quests
typedef struct {
  HASHTABLE  *quests; // the quests we're on, and data for them
  LIST    *completed; // names of quests that we've completed
} QUEST_AUX_DATA;

QUEST_AUX_DATA *newQuestAuxData(void) {
  QUEST_AUX_DATA *data = malloc(sizeof(QUEST_AUX_DATA));
  data->quests         = newHashtable();
  data->completed      = newList();
  return data;
}

void deleteQuestAuxData(QUEST_AUX_DATA *data) {
  if(hashSize(data->quests) > 0) {
    HASH_ITERATOR *quest_i = newHashIterator(data->quests);
    const char        *key = NULL;
    QUEST_PROGRESS  *quest = NULL;
    ITERATE_HASH(key, quest, quest_i) {
      deleteQuestProgress(quest);
    } deleteHashIterator(quest_i);
  }
  deleteHashtable(data->quests);
  deleteListWith(data->completed, free);
  free(data);
}

void questAuxDataCopyTo(QUEST_AUX_DATA *from, QUEST_AUX_DATA *to) {
  // empty our quest table, if neccessary
  if(hashSize(to->quests) > 0) {
    HASH_ITERATOR *quest_i = newHashIterator(to->quests);
    const char        *key = NULL;
    QUEST_PROGRESS  *quest = NULL;
    ITERATE_HASH(key, quest, quest_i) {
      deleteQuestProgress(hashRemove(to->quests, key));
    } deleteHashIterator(quest_i);
  }

  // copy over our current contents, if neccessary
  if(hashSize(from->quests) > 0) {
    HASH_ITERATOR *quest_i = newHashIterator(from->quests);
    const char        *key = NULL;
    QUEST_PROGRESS  *quest = NULL;
    ITERATE_HASH(key, quest, quest_i) {
      hashPut(to->quests, key, questProgressCopy(quest));
    } deleteHashIterator(quest_i);
  }

  deleteListWith(to->completed, free);
  to->completed = listCopyWith(from->completed, strdup);
}

QUEST_AUX_DATA *questAuxDataCopy(QUEST_AUX_DATA *data) {
  QUEST_AUX_DATA *newdata = newQuestAuxData();
  questAuxDataCopyTo(data, newdata);
  return newdata;
}

STORAGE_SET *store_quest_completed(const char *quest) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "quest", quest);
  return set;
}

STORAGE_SET *questAuxDataStore(QUEST_AUX_DATA *data) {
  STORAGE_SET *set = new_storage_set();

  // if we have quests on the go, store them
  if(hashSize(data->quests) > 0) {
    STORAGE_SET_LIST *quests = new_storage_list(); 
    HASH_ITERATOR   *quest_i = newHashIterator(data->quests);
    const char          *key = NULL;
    QUEST_PROGRESS    *quest = NULL;
    ITERATE_HASH(key, quest, quest_i) {
      STORAGE_SET *one_quest = new_storage_set();
      store_string(one_quest, "name", key);
      store_set(one_quest,    "data", questProgressStore(quest));
      storage_list_put(quests, one_quest);
    } deleteHashIterator(quest_i);
    store_list(set, "quests", quests);
  }

  store_list(set, "completed", 
	     gen_store_list(data->completed, store_quest_completed));
  return set;
}

char *read_quest_completed(STORAGE_SET *set) {
  return strdup(read_string(set, "quest"));
}

QUEST_AUX_DATA *questAuxDataRead(STORAGE_SET *set) {
  QUEST_AUX_DATA     *data = newQuestAuxData();
  STORAGE_SET_LIST *quests = read_list(set, "quests");
  STORAGE_SET   *one_quest = NULL;

  while( (one_quest = storage_list_next(quests)) != NULL) {
    hashPut(data->quests, read_string(one_quest, "name"),
	    questProgressRead(read_set(one_quest, "data")));
  }

  deleteList(data->completed);
  data->completed = gen_read_list(read_list(set, "completed"), 
				  read_quest_completed);
  return data;
}



//*****************************************************************************
// local datastructures, variables, defines
//*****************************************************************************

// a table of our different objective types, and functions we need for them
HASHTABLE *ob_type_table = NULL;

typedef struct {
  bool (* ob_ok)(QUEST_PROGRESS *, QUEST_OBJECTIVE *);
  void (* append_ob_status)(BUFFER *, QUEST_PROGRESS *, QUEST_OBJECTIVE *);
} OBJECTIVE_FUNCS;

struct quest_data {
  char        *key;
  char       *name;
  BUFFER     *desc;
  LIST     *stages;
};

struct quest_stage {
  char        *name;
  LIST  *objectives;
  BUFFER *endscript;
  QUEST_DATA *quest;
};

struct quest_objective {
  char            *type;
  char            *desc;
  HASHTABLE       *vars;
  QUEST_STAGE    *stage;
};

//
// create a new struct for holding our objective functions
OBJECTIVE_FUNCS *newObjectiveFuncs(
    bool            (* ob_ok)(QUEST_PROGRESS *, QUEST_OBJECTIVE *),
    void (* append_ob_status)(BUFFER *, QUEST_PROGRESS *, QUEST_OBJECTIVE *)) {
  OBJECTIVE_FUNCS *funcs = malloc(sizeof(OBJECTIVE_FUNCS));
  funcs->ob_ok = ob_ok;
  funcs->append_ob_status = append_ob_status;
  return funcs;
}



//*****************************************************************************
// implementation of quest.h
//*****************************************************************************
QUEST_DATA *newQuest(void) {
  QUEST_DATA *quest = malloc(sizeof(QUEST_DATA));
  quest->key        = strdup("");
  quest->name       = strdup("");
  quest->desc       = newBuffer(1);
  quest->stages     = newList();
  return quest;
}

void deleteQuest(QUEST_DATA *quest) {
  if(quest->key)    free(quest->key);
  if(quest->name)   free(quest->name);
  if(quest->desc)   deleteBuffer(quest->desc);
  if(quest->stages) deleteListWith(quest->stages, deleteQuestStage);
  free(quest);
}

void questCopyTo(QUEST_DATA *from, QUEST_DATA *to) {
  if(to->key)  free(to->key);
  if(to->name) free(to->name);  
  deleteListWith(to->stages, deleteQuestStage);
  bufferClear(to->desc);
  to->key  = strdupsafe(from->key);
  to->name = strdupsafe(from->name);
  bufferCat(to->desc, bufferString(from->desc));
  to->stages = listCopyWith(from->stages, questStageCopy);
  LIST_ITERATOR *stage_i = newListIterator(to->stages);
  QUEST_STAGE     *stage = NULL;
  ITERATE_LIST(stage, stage_i) {
    stage->quest = to;
  } deleteListIterator(stage_i);
}

QUEST_DATA *questCopy(QUEST_DATA *quest) {
  QUEST_DATA *newquest = newQuest();
  questCopyTo(quest, newquest);
  return newquest;
}

STORAGE_SET *questStore(QUEST_DATA *quest) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "name", questGetName(quest));
  store_string(set, "desc", questGetDesc(quest));
  store_list(set, "stages", gen_store_list(quest->stages, questStageStore));
  return set;
}

QUEST_DATA *questRead(STORAGE_SET *set) {
  QUEST_DATA *quest = newQuest();
  questSetName(quest, read_string(set, "name"));
  questSetDesc(quest, read_string(set, "desc"));
  LIST *stages = gen_read_list(read_list(set, "stages"), questStageRead);
  LIST_ITERATOR *stage_i = newListIterator(stages);
  QUEST_STAGE     *stage = NULL;
  ITERATE_LIST(stage, stage_i) {
    questAddStage(quest, stage);
  } deleteListIterator(stage_i);
  deleteList(stages);
  return quest;
}

void questSetKey(QUEST_DATA *quest, const char *key) {
  if(quest->key) free(quest->key);
  quest->key = strdupsafe(key);
}

void questSetName(QUEST_DATA *quest, const char *name) {
  if(quest->name) free(quest->name);
  quest->name = strdupsafe(name);
}

void questSetDesc(QUEST_DATA *quest, const char *desc) {
  bufferClear(quest->desc);
  bufferCat(quest->desc, desc);
}

const char *questGetKey(QUEST_DATA *quest) {
  return quest->key;
}

const char *questGetName(QUEST_DATA *quest) {
  return quest->name;
}

const char *questGetDesc(QUEST_DATA *quest) {
  return bufferString(quest->desc);
}

BUFFER *questGetDescBuf(QUEST_DATA *quest) {
  return quest->desc;
}

void questAddStage(QUEST_DATA *quest, QUEST_STAGE *stage) {
  listQueue(quest->stages, stage);
  stage->quest = quest;
}

void questRemoveStage(QUEST_DATA *quest, QUEST_STAGE *stage) {
  if(listRemove(quest->stages, stage))
    stage->quest = NULL;
}

QUEST_STAGE *questRemoveStageNum(QUEST_DATA *quest, int num) {
  QUEST_STAGE *stage = listGet(quest->stages, num);
  if(stage != NULL)
    questRemoveStage(quest, stage);
  return stage;
}

LIST *questGetStages(QUEST_DATA *quest) {
  return quest->stages;
}

QUEST_STAGE *newQuestStage(void) {
  QUEST_STAGE *stage = malloc(sizeof(QUEST_STAGE));
  stage->name        = strdupsafe("");
  stage->objectives  = newList();
  stage->endscript   = newBuffer(1);
  stage->quest       = NULL;
  return stage;
}

void deleteQuestStage(QUEST_STAGE *stage) {
  if(stage->name)       free(stage->name);
  if(stage->objectives) deleteListWith(stage->objectives, deleteQuestObjective);
  if(stage->endscript)  deleteBuffer(stage->endscript);
  free(stage);
}

void questStageCopyTo(QUEST_STAGE *from, QUEST_STAGE *to) {
  questStageSetName(to, questStageGetName(from));
  questStageSetEndScript(to, questStageGetEndScript(from));
  deleteListWith(to->objectives, deleteQuestObjective);
  to->objectives = listCopyWith(from->objectives, questObjectiveCopy);
  LIST_ITERATOR *ob_i = newListIterator(to->objectives);
  QUEST_OBJECTIVE *ob = NULL;
  ITERATE_LIST(ob, ob_i) {
    ob->stage = to;
  } deleteListIterator(ob_i);
}

QUEST_STAGE *questStageCopy(QUEST_STAGE *stage) {
  QUEST_STAGE *newstage = newQuestStage();
  questStageCopyTo(stage, newstage);
  return newstage;
}

STORAGE_SET *questStageStore(QUEST_STAGE *stage) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "name",      questStageGetName(stage));
  store_string(set, "endscript", questStageGetEndScript(stage));
  store_list(set, "objectives", gen_store_list(stage->objectives, 
					       questObjectiveStore));
  return set;
}

QUEST_STAGE *questStageRead(STORAGE_SET *set) {
  QUEST_STAGE *stage = newQuestStage();
  questStageSetName(stage, read_string(set, "name"));
  questStageSetEndScript(stage, read_string(set, "endscript"));
  LIST *obs = gen_read_list(read_list(set, "objectives"), questObjectiveRead);
  LIST_ITERATOR *ob_i = newListIterator(obs);
  QUEST_OBJECTIVE *ob = NULL;
  ITERATE_LIST(ob, ob_i) {
    questStageAddObjective(stage, ob);
  } deleteListIterator(ob_i);
  deleteList(obs);
  return stage;
}

void questStageSetName(QUEST_STAGE *stage, const char *name) {
  if(stage->name) free(stage->name);
  stage->name = strdupsafe(name);
}

void questStageSetEndScript(QUEST_STAGE *stage, const char *script) {
  bufferClear(stage->endscript);
  bufferCat(stage->endscript, script);
}

const char *questStageGetName(QUEST_STAGE *stage) {
  return stage->name;
}

const char *questStageGetEndScript(QUEST_STAGE *stage) {
  return bufferString(stage->endscript);
}

BUFFER *questStageGetEndScriptBuf(QUEST_STAGE *stage) {
  return stage->endscript;
}

void questStageAddObjective(QUEST_STAGE *stage, QUEST_OBJECTIVE *ob) {
  listQueue(stage->objectives, ob);
  ob->stage = stage;
}

LIST *questStageGetObjectives(QUEST_STAGE *stage) {
  return stage->objectives;
}

void questStageRemoveObjective(QUEST_STAGE *stage, QUEST_OBJECTIVE *ob) {
  if(listRemove(stage->objectives, ob))
    ob->stage = NULL;
}

QUEST_OBJECTIVE *questStageRemoveObjectiveNum(QUEST_STAGE *stage, int num) {
  QUEST_OBJECTIVE *ob = listGet(stage->objectives, num);
  if(ob != NULL)
    questStageRemoveObjective(stage, ob);
  return ob;
}

QUEST_DATA *questStageGetQuest(QUEST_STAGE *stage) {
  return stage->quest;
}

QUEST_OBJECTIVE *newQuestObjective(void) {
  QUEST_OBJECTIVE *ob = malloc(sizeof(QUEST_OBJECTIVE));
  ob->vars            = newHashtable();
  ob->type            = strdup("");
  ob->desc            = strdup("");
  ob->stage           = NULL;
  return ob;
}

void deleteQuestObjective(QUEST_OBJECTIVE *ob) {
  if(ob->type)  free(ob->type);
  if(ob->desc)  free(ob->desc);
  if(ob->vars) {
    if(hashSize(ob->vars) > 0) {
      HASH_ITERATOR *hash_i = newHashIterator(ob->vars);
      const char       *key = NULL;
      char             *val = NULL;
      ITERATE_HASH(key, val, hash_i) {
	free(val);
      } deleteHashIterator(hash_i);
    }
    deleteHashtable(ob->vars);
  }
  free(ob);
}

void questObjectiveCopyTo(QUEST_OBJECTIVE *from, QUEST_OBJECTIVE *to) {
  if(to->type) free(to->type);
  if(to->desc) free(to->desc);
  // if we have contents at the moment, remove them
  if(hashSize(to->vars) > 0) {
    HASH_ITERATOR *var_i = newHashIterator(to->vars);
    const char      *key = NULL;
    char            *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      hashRemove(to->vars, key);
      free(val);
    } deleteHashIterator(var_i);
  }

  // if from has contents, copy them
  if(hashSize(from->vars) > 0) {
    HASH_ITERATOR *var_i = newHashIterator(from->vars);
    const char      *key = NULL;
    char            *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      hashPut(to->vars, key, strdup(val));
    } deleteHashIterator(var_i);
  }
  to->type  = strdupsafe(from->type);
  to->desc  = strdupsafe(from->desc);
}

QUEST_OBJECTIVE *questObjectiveCopy(QUEST_OBJECTIVE *ob) {
  QUEST_OBJECTIVE *newob = newQuestObjective();
  questObjectiveCopyTo(ob, newob);
  return newob;
}

STORAGE_SET *questObjectiveStore(QUEST_OBJECTIVE *ob) {
  STORAGE_SET *set = new_storage_set();
  store_string(set, "type", questObjectiveGetType(ob));
  store_string(set, "desc", questObjectiveGetDesc(ob));
  if(hashSize(ob->vars) > 0) {
    STORAGE_SET_LIST *list = new_storage_list();
    HASH_ITERATOR   *var_i = newHashIterator(ob->vars);
    const char        *key = NULL;
    char              *val = NULL;
    ITERATE_HASH(key, val, var_i) {
      STORAGE_SET *one_var = new_storage_set();
      store_string(one_var, "key", key);
      store_string(one_var, "val", val);
      storage_list_put(list, one_var);
    } deleteHashIterator(var_i);
    store_list(set, "vars", list);
  }
  return set;
}

QUEST_OBJECTIVE *questObjectiveRead(STORAGE_SET *set) {
  QUEST_OBJECTIVE    *ob = newQuestObjective();
  questObjectiveSetType(ob, read_string(set, "type"));
  questObjectiveSetDesc(ob, read_string(set, "desc"));
  STORAGE_SET_LIST *vars = read_list(set, "vars");
  STORAGE_SET   *one_var = NULL;
  while( (one_var = storage_list_next(vars)) != NULL) {
    hashPut(ob->vars, read_string(one_var, "key"),
	    strdup(read_string(one_var, "val")));
  }
  return ob;
}

const char *questObjectiveGetType(QUEST_OBJECTIVE *ob) {
  return ob->type;
}

const char *questObjectiveGetDesc(QUEST_OBJECTIVE *ob) {
  return ob->desc;
}

HASHTABLE *questObjectiveGetVars(QUEST_OBJECTIVE *ob) {
  return ob->vars;
}

const char *questObjectiveGetVar(QUEST_OBJECTIVE *ob, const char *var) {
  const char *val = hashGet(ob->vars, var);
  return (val ? val : "");
}

int questObjectiveGetVarInt(QUEST_OBJECTIVE *ob, const char *var) {
  return atoi(questObjectiveGetVar(ob, var));
}

void questObjectiveSetType(QUEST_OBJECTIVE *ob, const char *type) {
  if(ob->type) free(ob->type);
  ob->type = strdupsafe(type);
}

void questObjectiveSetDesc(QUEST_OBJECTIVE *ob, const char *desc) {
  if(ob->desc) free(ob->desc);
  ob->desc = strdupsafe(desc);
}

void questObjectiveSetVar(QUEST_OBJECTIVE *ob, const char *var,const char *val){
  questObjectiveDeleteVar(ob, var);
  hashPut(ob->vars, var, strdupsafe(val));
}

void questObjectiveSetVarInt(QUEST_OBJECTIVE *ob, const char *var, int val) {
  char buf[20];
  sprintf(buf, "%d", val);
  hashPut(ob->vars, var, strdup(buf));
}

void questObjectiveDeleteVar(QUEST_OBJECTIVE *ob, const char *var) {
  if(hashIn(ob->vars, var))
    free(hashRemove(ob->vars, var));
}

void questObjectiveClearVars(QUEST_OBJECTIVE *ob) {
  LIST           *vars = hashCollect(ob->vars);
  LIST_ITERATOR *var_i = newListIterator(vars);
  char            *var = NULL;
  ITERATE_LIST(var, var_i) {
    free(hashRemove(ob->vars, var));
  } deleteListIterator(var_i);
  deleteListWith(vars, free);
}

QUEST_STAGE *questObjectiveGetStage(QUEST_OBJECTIVE *ob) {
  return ob->stage;
}

void charStartQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  charCancelQuest(ch, quest);
  hashPut(data->quests, questGetKey(quest), newQuestProgress());
  send_to_char(ch, "{pYou gain the quest, %s{n\r\n", questGetName(quest));
}

void charFailQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashGet(data->quests, questGetKey(quest));
  if(prog != NULL)
    prog->failed = TRUE;
}

void charCancelQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  char      *completed = listRemoveWith(data->completed, quest->key,strcasecmp);
  QUEST_PROGRESS *prog = hashRemove(data->quests, quest->key);
  if(completed) free(completed);
  if(prog)      deleteQuestProgress(prog);
}

void charAdvanceQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashRemove(data->quests, questGetKey(quest));
  if(prog != NULL) {
    // run the advancement script
    QUEST_STAGE *stage = listGet(questGetStages(quest), prog->stage);
    if(stage != NULL && *questStageGetEndScript(stage)) {
      PyObject *dict = restricted_script_dict();
      PyObject *pych = charGetPyForm(ch);
      PyDict_SetItemString(dict, "ch", pych);

      // run the script
      run_script(dict, questStageGetEndScript(stage), 
		 get_key_locale(questGetKey(quest)));

      // garbage collection
      Py_DECREF(dict);
      Py_DECREF(pych);
    }

    // are we on the last stage of the quest?
    if(listSize(questGetStages(quest)) == prog->stage+1) {
      listQueue(data->completed, strdupsafe(questGetKey(quest)));
      send_to_char(ch, "{pYou complete the quest, %s{n\r\n", 
		   questGetName(quest));
      hookRun("complete_quest", hookBuildInfo("ch str", ch,questGetKey(quest)));
    }
    else {
      QUEST_PROGRESS *newprog = newQuestProgress();
      newprog->stage = prog->stage + 1;
      hashPut(data->quests, questGetKey(quest), newprog);
      send_to_char(ch, "{pYou advance on the quest, %s{n\r\n", 
		   questGetName(quest));
      hookRun("advance_quest", hookBuildInfo("ch str", ch, questGetKey(quest)));
    }

    // free up our old progress data
    deleteQuestProgress(prog);
  }
}

bool charCompletedQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  return (listGetWith(data->completed, questGetKey(quest), strcasecmp) != NULL);
}

bool charOnQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  return hashIn(data->quests, questGetKey(quest));
}

int charGetQuestStage(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashGet(data->quests, questGetKey(quest));
  return (prog ? prog->stage : -1);
}

bool charFailedQuest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashGet(data->quests, questGetKey(quest));
  return (prog ? prog->failed : FALSE);
}



//*****************************************************************************
// local functions
//*****************************************************************************

//
// check to see if we've completed a kill objective
bool kill_objective_ok(QUEST_PROGRESS *prog, QUEST_OBJECTIVE *ob) {
  char var[SMALL_BUFFER];
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  const char *enemy = get_fullkey_relative(questObjectiveGetVar(ob, "enemy"),
					   get_key_locale(questGetKey(quest)));
  sprintf(var, "kill_%s", enemy);
  return (questProgressGetVarInt(prog, var) >= 
	  questObjectiveGetVarInt(ob, "times"));
}

//
// check to see if we've completed a greet objective
bool greet_objective_ok(QUEST_PROGRESS *prog, QUEST_OBJECTIVE *ob) {
  char var[SMALL_BUFFER];
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  const char   *tgt = get_fullkey_relative(questObjectiveGetVar(ob, "person"),
					   get_key_locale(questGetKey(quest)));
  sprintf(var, "approach_%s", tgt);
  return (questProgressGetVarInt(prog, var) == 1);
}

//
// check to see if we've completed an indefinite objective (we haven't)
bool indefinite_objective_ok(QUEST_PROGRESS *prog, QUEST_OBJECTIVE *ob) {
  return FALSE;
}

//
// check to see if we've completed a give objective
bool give_objective_ok(QUEST_PROGRESS *prog, QUEST_OBJECTIVE *ob) {
  char var[SMALL_BUFFER];
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  char    *receiver = 
    strdup(get_fullkey_relative(questObjectiveGetVar(ob, "person"),
				get_key_locale(questGetKey(quest))));
  char    *item = 
    strdup(get_fullkey_relative(questObjectiveGetVar(ob, "item"),
				get_key_locale(questGetKey(quest))));
  sprintf(var, "give_%s_%s", item, receiver);

  // garbage collection
  free(receiver);
  free(item);

  return (questProgressGetVarInt(prog, var) >=
	  questObjectiveGetVarInt(ob, "count"));
}

//
// returns whether or not the quest progress has said the objective is done
bool objective_ok(QUEST_PROGRESS *prog, QUEST_OBJECTIVE *ob) {
  OBJECTIVE_FUNCS *funcs = hashGet(ob_type_table, questObjectiveGetType(ob));
  if(funcs == NULL || funcs->ob_ok == NULL)
    return FALSE;
  else
    return funcs->ob_ok(prog, ob);
}

//
// looks at a character's progress on a quest. If all of the current stage's
// objectives are met, then advance the character on the quest
void try_advance_quest(CHAR_DATA *ch, QUEST_DATA *quest) {
  QUEST_AUX_DATA *data = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashGet(data->quests, questGetKey(quest));
  if(prog != NULL) {
    QUEST_STAGE *stage = listGet(questGetStages(quest), prog->stage);
    if(stage != NULL) {
      bool         obs_ok = TRUE;
      LIST_ITERATOR *ob_i = newListIterator(stage->objectives);
      QUEST_OBJECTIVE *ob = NULL;
      ITERATE_LIST(ob, ob_i) {
	obs_ok = objective_ok(prog, ob);
	if(obs_ok == FALSE)
	  break;
      } deleteListIterator(ob_i);
      if(obs_ok == TRUE)
	charAdvanceQuest(ch, quest);
    }
  }
}

//
// returns a list of all the objectives the character currently has. The list
// must be deleted after use
LIST *charGetQuestObjectives(CHAR_DATA *ch) {
  LIST       *objectives = newList();
  QUEST_AUX_DATA    *aux = charGetAuxiliaryData(ch, "quest_data");
  HASH_ITERATOR  *prog_i = newHashIterator(aux->quests);
  const char        *key = NULL;
  QUEST_PROGRESS   *prog = NULL;
  // go across all of the quests we're currently on
  ITERATE_HASH(key, prog, prog_i) {
    QUEST_DATA *quest = worldGetType(gameworld, "quest", key);
    if(quest != NULL) {
      // figure out what our current objectives are in the quest
      QUEST_STAGE *stage = listGet(questGetStages(quest), prog->stage);
      if(stage != NULL) {
	LIST_ITERATOR *ob_i = newListIterator(stage->objectives);
	QUEST_OBJECTIVE *ob = NULL;
	ITERATE_LIST(ob, ob_i) {
	  listQueue(objectives, ob);
	} deleteListIterator(ob_i);
      }
    }
  } deleteHashIterator(prog_i);
  return objectives;
}

//
// compares two quests by name
int questnamecmp(QUEST_DATA *q1, QUEST_DATA *q2) {
  return strcasecmp(questGetName(q1), questGetName(q2));
}

//
// return a list of quests (in order by completion status, and then alphabetical
// order) of the quests the character is completing or has completed.
LIST *charGetQuests(CHAR_DATA *ch) {
  QUEST_AUX_DATA *aux = charGetAuxiliaryData(ch, "quest_data");
  LIST   *master_list = newList();
  LIST     *prog_list = newList();
  LIST     *comp_list = newList();
  QUEST_DATA   *quest = NULL;

  // get all of the quests we are currently on
  HASH_ITERATOR *prog_i = newHashIterator(aux->quests);
  const char       *key = NULL;
  QUEST_PROGRESS  *prog = NULL;
  ITERATE_HASH(key, prog, prog_i) {
    if((quest = worldGetType(gameworld, "quest", key)) != NULL)
      listPutWith(prog_list, quest, questnamecmp);
  } deleteHashIterator(prog_i);

  // get all of the quests we've completed
  LIST_ITERATOR *comp_i = newListIterator(aux->completed);
  ITERATE_LIST(key, comp_i) {
    if((quest = worldGetType(gameworld, "quest", key)) != NULL)
      listPutWith(comp_list, quest, questnamecmp);
  } deleteListIterator(comp_i);

  // append the contents of both list to our master list
  while( (quest = listPop(prog_list)) != NULL)
    listQueue(master_list, quest);
  while( (quest = listPop(comp_list)) != NULL)
    listQueue(master_list, quest);

  // garbage collection
  deleteList(prog_list);
  deleteList(comp_list);
  return master_list;
}

//
// appends the kill status on a quest objective
void append_kill_objective_status(BUFFER *buf, QUEST_PROGRESS *prog,
				  QUEST_OBJECTIVE *ob) {
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  char var[SMALL_BUFFER];
  sprintf(var, "kill_%s", 
	  get_fullkey_relative(questObjectiveGetVar(ob, "enemy"),
			       get_key_locale(questGetKey(quest))));
  bprintf(buf, "%d/%d", 
	  questProgressGetVarInt(prog, var), 
	  questObjectiveGetVarInt(ob, "times"));
}

//
// appends the greet status on a quest objective
void append_greet_objective_status(BUFFER *buf, QUEST_PROGRESS *prog,
				  QUEST_OBJECTIVE *ob) {
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  char var[SMALL_BUFFER];
  sprintf(var, "approach_%s", 
	  get_fullkey_relative(questObjectiveGetVar(ob, "person"),
			       get_key_locale(questGetKey(quest))));
  bprintf(buf, "%s", (questProgressGetVarInt(prog, var) == 1 ? "complete" : "incomplete"));
}

//
// appends the indefinite status on a quest objective
void append_indefinite_objective_status(BUFFER *buf, QUEST_PROGRESS *prog,
					QUEST_OBJECTIVE *ob) {
  bprintf(buf, "incomplete");
}

//
// appends the give status on a quest objective
void append_give_objective_status(BUFFER *buf, QUEST_PROGRESS *prog,
				  QUEST_OBJECTIVE *ob) {
  QUEST_DATA *quest = questStageGetQuest(questObjectiveGetStage(ob));
  char var[SMALL_BUFFER];
  sprintf(var, "give_%s_", 
	  get_fullkey_relative(questObjectiveGetVar(ob, "item"),
			       get_key_locale(questGetKey(quest))));
  sprintf(var+strlen(var), "%s",
	  get_fullkey_relative(questObjectiveGetVar(ob, "person"),
			       get_key_locale(questGetKey(quest))));
  bprintf(buf, "%d/%d", 
	  questProgressGetVarInt(prog, var), 
	  questObjectiveGetVarInt(ob, "count"));
}

//
// appends info about the status of a quest objective to the buffer
void append_objective_info(BUFFER *buf, QUEST_PROGRESS *prog, 
			   QUEST_OBJECTIVE *ob) {
  OBJECTIVE_FUNCS *funcs = hashGet(ob_type_table, questObjectiveGetType(ob));
  bprintf(buf, "%s: {w", questObjectiveGetDesc(ob));
  if(funcs == NULL || funcs->append_ob_status == NULL)
    bprintf(buf, "incomplete");
  else
    funcs->append_ob_status(buf, prog, ob);
}

//
// shows the character his status on a quest
void show_incomplete_quest_to_char(QUEST_DATA *quest, CHAR_DATA *ch) {
  QUEST_AUX_DATA  *aux = charGetAuxiliaryData(ch, "quest_data");
  QUEST_PROGRESS *prog = hashGet(aux->quests, questGetKey(quest));
  QUEST_STAGE   *stage = listGet(questGetStages(quest), prog->stage);
  BUFFER          *buf = newBuffer(MAX_BUFFER);
  BUFFER         *dbuf = bufferCopy(questGetDescBuf(quest));
  bufferFormat(dbuf, SCREEN_WIDTH, PARA_INDENT);

  // put up our basic info
  bprintf(buf,
	  "{c%s%s\r\n"
	  "{g%s\r\n"
	  "{wObjectives:{g\r\n",
	  questGetName(quest), (charFailedQuest(ch, quest) ? "{R - FAILED":""),
	  bufferString(dbuf));

  // append info for each of our objectives
  LIST_ITERATOR *ob_i = newListIterator(questStageGetObjectives(stage));
  QUEST_OBJECTIVE *ob = NULL;
  ITERATE_LIST(ob, ob_i) {
    bprintf(buf, "  {c");
    append_objective_info(buf, prog, ob);
    bprintf(buf, "{n\r\n");
  } deleteListIterator(ob_i);
  bprintf(buf, "{n");

  // send the info
  if(charGetSocket(ch))
    page_string(charGetSocket(ch), bufferString(buf));
  
  // garbage collection
  deleteBuffer(buf);
  deleteBuffer(dbuf);
}

//
// shows the character info about a quest he has completed
void show_complete_quest_to_char(QUEST_DATA *quest, CHAR_DATA *ch) {
  BUFFER  *buf = newBuffer(MAX_BUFFER);
  BUFFER *dbuf = bufferCopy(questGetDescBuf(quest));
  bufferFormat(dbuf, SCREEN_WIDTH, PARA_INDENT);
  bprintf(buf, 
	  "{c%s\r\n"
	  "{g%s\r\n"
	  "{wYou have completed this quest.{n\r\n",
	  questGetName(quest), bufferString(dbuf));
  
  // send the info
  if(charGetSocket(ch))
    page_string(charGetSocket(ch), bufferString(buf));
  
  // garbage collection
  deleteBuffer(buf);
  deleteBuffer(dbuf);
}

//
// shows info for one quest to a character
void show_quest_to_char(QUEST_DATA *quest, CHAR_DATA *ch) {
  // have we already completed the quest?
  if(charCompletedQuest(ch, quest))
    show_complete_quest_to_char(quest, ch);
  // we're currently on the quest
  else if(charOnQuest(ch, quest))
    show_incomplete_quest_to_char(quest, ch);
  // we've never been on the quest
  else
    send_to_char(ch, "You know nothing about %s.\r\n", questGetName(quest));
}



//*****************************************************************************
// player commands
//*****************************************************************************

//
// Display info about the quests we have going on at the moment
COMMAND(cmd_quests) {
  int num = -1;
  if(!parse_args(ch, TRUE, cmd, arg, "| [info] int", &num))
    return;

  // show info for a specific quest
  else if(num >= 0) {
    LIST      *quests = charGetQuests(ch);
    QUEST_DATA *quest = listGet(quests, num);
    if(quest == NULL)
      send_to_char(ch, "You have no quest numbered %d!\r\n", num);
    else
      show_quest_to_char(quest, ch);
    // garbage collection
    deleteList(quests);
  }

  // show all quests we currently have recorded
  else {
    BUFFER            *buf = newBuffer(MAX_BUFFER);
    LIST           *quests = charGetQuests(ch);
    LIST_ITERATOR *quest_i = newListIterator(quests);
    QUEST_DATA      *quest = NULL;
    int              count = 0;

    bprintf(buf, "{w%-70s %9s\r\n"
	    "{b--------------------------------------------------------------------------------\r\n", " Quest", "Status "); 

    // build a list of the quests we need to complete
    ITERATE_LIST(quest, quest_i) {
      if(charFailedQuest(ch, quest))
	bprintf(buf,"{c %3d) %-63s    FAILED \r\n", count, questGetName(quest));
      else if(charOnQuest(ch, quest))
	bprintf(buf,"{c %3d) %-63sINCOMPLETE \r\n", count, questGetName(quest));
      else
	bprintf(buf,"{c %3d) %-63s  COMPLETE \r\n", count, questGetName(quest));
      count++;
    } deleteListIterator(quest_i);

    // page our string
    if(charGetSocket(ch)) {
      if(listSize(quests) > 0) {
	bprintf(buf, "\r\n{gTo view info for a specific quest, use: "
		"quest info <num>{n\r\n");
	page_string(charGetSocket(ch), bufferString(buf));
      }
      else
	send_to_char(ch, "You have never been on a quest.\r\n");
    }
  
    // garbage collection
    deleteList(quests);
    deleteBuffer(buf);
  }
}

//
// used to let an admin force-start a quest for a person
COMMAND(cmd_qstart) {
  CHAR_DATA    *tgt = ch;
  char         *key = NULL;
  QUEST_DATA *quest = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word | ch.world", &key, &tgt))
    return;

  quest = worldGetType(gameworld, "quest", 
		       get_fullkey_relative(key, 
			   get_key_locale(roomGetClass(charGetRoom(ch)))));
  if(quest == NULL)
    send_to_char(ch, "The quest, %s, does not exist.\r\n", key);
  else {
    send_to_char(ch, "Ok.\r\n");
    charStartQuest(tgt, quest);
  }
}

//
// cancel a quest in progress/completed quest
COMMAND(cmd_qcancel) {
  CHAR_DATA    *tgt = ch;
  char         *key = NULL;
  QUEST_DATA *quest = NULL;
  if(!parse_args(ch, TRUE, cmd, arg, "word | ch.world", &key, &tgt))
    return;

  quest = worldGetType(gameworld, "quest", 
		       get_fullkey_relative(key, 
			   get_key_locale(roomGetClass(charGetRoom(ch)))));
  if(quest == NULL)
    send_to_char(ch, "The quest, %s, does not exist.\r\n", key);
  else {
    send_to_char(ch, "Ok.\r\n");
    charCancelQuest(tgt, quest);
  }
}



//*****************************************************************************
// hooks
//*****************************************************************************

//
// whenever we kill something, see if it increases our progress on one of
// our current quests
void quest_kill_hook(const char *info) {
  CHAR_DATA   *ch = NULL;
  CHAR_DATA *vict = NULL;
  hookParseInfo(info, &ch, &vict);

  LIST           *obs = charGetQuestObjectives(ch);
  LIST_ITERATOR *ob_i = newListIterator(obs);
  QUEST_OBJECTIVE *ob = NULL;
  QUEST_AUX_DATA *aux = charGetAuxiliaryData(ch, "quest_data");
  ITERATE_LIST(ob, ob_i) {
    QUEST_STAGE   *stage = questObjectiveGetStage(ob);
    QUEST_DATA    *quest = questStageGetQuest(stage);
    QUEST_PROGRESS *prog = hashGet(aux->quests, questGetKey(quest));
    if(prog->failed)
      continue;
    if(!strcasecmp("kill", questObjectiveGetType(ob))) {
      const char *enemy = 
	get_fullkey_relative(questObjectiveGetVar(ob, "enemy"),
			     get_key_locale(questGetKey(quest)));
      // if it's the right enemy, up our progress and try to advance
      if(charIsInstance(vict, enemy)) {
	char var[SMALL_BUFFER];
	sprintf(var, "kill_%s", enemy);
	int val = questProgressGetVarInt(prog, var) + 1;
	int max = questObjectiveGetVarInt(ob, "times");
	questProgressSetVarInt(prog, var, MIN(val, max));
	try_advance_quest(ch, quest);
      }
    }
  } deleteListIterator(ob_i);
  deleteList(obs);
}

//
// whenever we greet someone, see if it increases our progress on one of
// our current quests
void quest_greet_hook(const char *info) {
  CHAR_DATA   *ch = NULL;
  CHAR_DATA  *tgt = NULL;
  hookParseInfo(info, &ch, &tgt);

  LIST           *obs = charGetQuestObjectives(ch);
  LIST_ITERATOR *ob_i = newListIterator(obs);
  QUEST_OBJECTIVE *ob = NULL;
  QUEST_AUX_DATA *aux = charGetAuxiliaryData(ch, "quest_data");
  ITERATE_LIST(ob, ob_i) {
    QUEST_STAGE   *stage = questObjectiveGetStage(ob);
    QUEST_DATA    *quest = questStageGetQuest(stage);
    QUEST_PROGRESS *prog = hashGet(aux->quests, questGetKey(quest));
    if(prog->failed)
      continue;
    if(!strcasecmp("approach", questObjectiveGetType(ob))) {
      const char *target = 
	get_fullkey_relative(questObjectiveGetVar(ob, "person"),
			     get_key_locale(questGetKey(quest)));
      // if it's the right person, up our progress and try to advance
      if(charIsInstance(tgt, target)) {
	char var[SMALL_BUFFER];
	sprintf(var, "approach_%s", target);
	questProgressSetVarInt(prog, var, 1);
	try_advance_quest(ch, quest);
      }
    }
  } deleteListIterator(ob_i);
  deleteList(obs);
}

//
// whenever we give an object to someone, see if it advances any of our quests
void quest_give_hook(const char *info) {
  CHAR_DATA   *ch = NULL;
  CHAR_DATA *recv = NULL;
  OBJ_DATA   *obj = NULL;
  hookParseInfo(info, &ch, &recv, &obj);

  LIST           *obs = charGetQuestObjectives(ch);
  LIST_ITERATOR *ob_i = newListIterator(obs);
  QUEST_OBJECTIVE *ob = NULL;
  QUEST_AUX_DATA *aux = charGetAuxiliaryData(ch, "quest_data");
  ITERATE_LIST(ob, ob_i) {
    QUEST_STAGE   *stage = questObjectiveGetStage(ob);
    QUEST_DATA    *quest = questStageGetQuest(stage);
    QUEST_PROGRESS *prog = hashGet(aux->quests, questGetKey(quest));
    if(prog->failed)
      continue;
    if(!strcasecmp("give", questObjectiveGetType(ob))) {
      char var[SMALL_BUFFER];
      sprintf(var, "give_");

      // make sure our object matches
      const char *obj_key = 
	get_fullkey_relative(questObjectiveGetVar(ob, "item"),
			     get_key_locale(questGetKey(quest)));
      if(!objIsInstance(obj, obj_key))
	continue;
      else
	sprintf(var+strlen(var), "%s_", obj_key);

      // make sure our receiver matches
      const char *recv_key =
	get_fullkey_relative(questObjectiveGetVar(ob, "person"),
			     get_key_locale(questGetKey(quest)));
      if(!charIsInstance(recv, recv_key))
	continue;
      else
	sprintf(var+strlen(var), "%s", recv_key);

      // up our progress, and try advancing in the quest
      int val = questProgressGetVarInt(prog, var) + 1;
      int max = questObjectiveGetVarInt(ob, "count");
      questProgressSetVarInt(prog, var, MIN(val, max));
      try_advance_quest(ch, quest);
    }
  } deleteListIterator(ob_i);
  deleteList(obs);
}



//*****************************************************************************
// python extensions
//*****************************************************************************
PyObject *PyChar_CompletedQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }

  return Py_BuildValue("i", charCompletedQuest(ch, quest));
}


PyObject *PyChar_IsOnQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  
  return Py_BuildValue("i", charOnQuest(ch, quest));
}


PyObject *PyChar_InvolvedQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  
  return Py_BuildValue("i", (charOnQuest(ch, quest) || 
			     charCompletedQuest(ch, quest)));
}


PyObject *PyChar_AdvanceQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  if(!charOnQuest(ch, quest)) {
    PyErr_Format(PyExc_StandardError, "Cannot advance character on quest, %s, "
		 "if character has not started the quest!", questGetKey(quest));
    return NULL;
  }

  charAdvanceQuest(ch, quest);
  return Py_BuildValue("i", 1);
}


PyObject *PyChar_FailQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  if(!charOnQuest(ch, quest)) {
    PyErr_Format(PyExc_StandardError, "Cannot fail character on quest, %s, "
		 "if character has not started the quest!", questGetKey(quest));
    return NULL;
  }

  charFailQuest(ch, quest);
  return Py_BuildValue("i", 1);
}


PyObject *PyChar_CancelQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  
  charCancelQuest(ch, quest);
  return Py_BuildValue("i", 1);
}


PyObject *PyChar_StartQuest(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  if(charOnQuest(ch, quest) || charCompletedQuest(ch, quest)) {
    PyErr_Format(PyExc_StandardError, "Character has already started quest, %s",
		 questGetKey(quest));
    return NULL;
  }

  charStartQuest(ch, quest);
  return Py_BuildValue("i", 1);
}


PyObject *PyChar_QuestStage(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  
  return Py_BuildValue("i", charGetQuestStage(ch, quest));
}


PyObject *PyChar_QuestFailed(PyObject *self, PyObject *args) {
  CHAR_DATA *ch = PyChar_AsChar(self);
  char     *key = NULL;
  if(!PyArg_ParseTuple(args, "s", &key)) {
    PyErr_Format(PyExc_TypeError, "A quest string key must be provided.");
    return NULL;
  }
  if(ch == NULL) {
    PyErr_Format(PyExc_StandardError, 
		 "Char %d does not exist.", PyChar_AsUid(self)); 
    return NULL;
  }

  QUEST_DATA *quest = 
    worldGetType(gameworld, "quest",
		 get_fullkey_relative(key, get_script_locale()));

  if(quest == NULL) {
    PyErr_Format(PyExc_StandardError, "Quest, %s, does not exist", key);
    return NULL;
  }
  
  return Py_BuildValue("i", charFailedQuest(ch, quest));
}



//*****************************************************************************
// initialization
//*****************************************************************************
void init_quests(void) {
  // set up our local variables
  ob_type_table = newHashtable();

  // set up our new datatype
  worldAddType(gameworld, "quest", questRead, questStore, deleteQuest, 
  	       questSetKey);

  // set up our auxiliary data
  auxiliariesInstall("quest_data",
		     newAuxiliaryFuncs(AUXILIARY_TYPE_CHAR,
				       newQuestAuxData,    deleteQuestAuxData,
				       questAuxDataCopyTo, questAuxDataCopy,
				       questAuxDataStore,  questAuxDataRead));

  // add our Python extensions
  PyChar_addMethod("quest_start",    PyChar_StartQuest,     METH_VARARGS, NULL);
  PyChar_addMethod("quest_cancel",   PyChar_CancelQuest,    METH_VARARGS, NULL);
  PyChar_addMethod("quest_advance",  PyChar_AdvanceQuest,   METH_VARARGS, NULL);
  PyChar_addMethod("quest_on",       PyChar_IsOnQuest,      METH_VARARGS, NULL);
  PyChar_addMethod("quest_completed",PyChar_CompletedQuest, METH_VARARGS, NULL);
  PyChar_addMethod("quest_involved", PyChar_InvolvedQuest,  METH_VARARGS, NULL);
  PyChar_addMethod("quest_stage",    PyChar_QuestStage,     METH_VARARGS, NULL);
  PyChar_addMethod("quest_failed",   PyChar_QuestFailed,    METH_VARARGS, NULL);
  PyChar_addMethod("quest_fail",     PyChar_FailQuest,      METH_VARARGS, NULL);

  // attach hooks
  hookAdd("post_death", quest_kill_hook);
  hookAdd("give",       quest_give_hook);
  hookAdd("greet",      quest_greet_hook);

  // add our functions for handling objective types
  hashPut(ob_type_table, "kill", 
	  newObjectiveFuncs(kill_objective_ok, append_kill_objective_status));
  hashPut(ob_type_table, "give", 
	  newObjectiveFuncs(give_objective_ok, append_give_objective_status));
  hashPut(ob_type_table, "approach",
	  newObjectiveFuncs(greet_objective_ok, append_greet_objective_status));
  hashPut(ob_type_table, "indefinite",
	  newObjectiveFuncs(indefinite_objective_ok, append_indefinite_objective_status));

  // add our commands
  add_cmd("quests",  NULL, cmd_quests,  "player", FALSE);
  add_cmd("qstart",  NULL, cmd_qstart,  "admin",  FALSE);
  add_cmd("qcancel", NULL, cmd_qcancel, "admin",  FALSE);

  // set up quest OLC
  init_qedit();
}
