/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file load.c
 *
 * @brief Contains stuff to load a pilot or look up information about it.
 */


#include "load.h"

#include "naev.h"

#include "nxml.h"
#include "log.h"
#include "player.h"
#include "nfile.h"
#include "array.h"
#include "space.h"
#include "toolkit.h"
#include "menu.h"
#include "dialogue.h"
#include "event.h"
#include "mission.h"
#include "faction.h"
#include "gui.h"
#include "unidiff.h"
#include "nlua_var.h"
#include "land.h"


#define LOAD_WIDTH      400 /**< Load window width. */
#define LOAD_HEIGHT     300 /**< Load window height. */

#define BUTTON_WIDTH    50 /**< Button width. */
#define BUTTON_HEIGHT   30 /**< Button height. */


static nsave_t *load_saves = NULL; /**< Array of save.s */


/*
 * Prototypes.
 */
/* externs */
/* player.c */
extern Planet* player_load( xmlNodePtr parent ); /**< Loads player related stuff. */
/* mission.c */
extern int missions_loadActive( xmlNodePtr parent ); /**< Loads active missions. */
/* event.c */
extern int events_loadActive( xmlNodePtr parent );
/* nlua_var.c */
extern int var_load( xmlNodePtr parent ); /**< Loads mission variables. */
/* faction.c */
extern int pfaction_load( xmlNodePtr parent ); /**< Loads faction data. */
/* hook.c */
extern int hook_load( xmlNodePtr parent ); /**< Loads hooks. */
/* space.c */
extern int space_sysLoad( xmlNodePtr parent ); /**< Loads the space stuff. */
/* unidiff.c */
extern int diff_load( xmlNodePtr parent ); /**< Loads the universe diffs. */
/* static */
static void load_menu_close( unsigned int wdw, char *str );
static void load_menu_load( unsigned int wdw, char *str );
static void load_menu_delete( unsigned int wdw, char *str );
static int load_load( nsave_t *save, const char *path );


/**
 * @brief Loads an individual save.
 */
static int load_load( nsave_t *save, const char *path )
{
   xmlDocPtr doc;
   xmlNodePtr root, parent, node;

   memset( save, 0, sizeof(nsave_t) );

   /* Load the XML. */
   doc   = xmlParseFile(path);
   if (doc == NULL) {
      WARN("Unable to parse save path '%s'.", path);
      return -1;
   }
   root = doc->xmlChildrenNode; /* base node */
   if (root == NULL) {
      WARN("Unable to get child node of save '%s'.",path);
      xmlFreeDoc(doc);
      return -1;
   }

   /* Save path. */
   save->path = strdup(path);

   /* Iterate inside the naev_save. */
   parent = root->xmlChildrenNode;
   do {
      xml_onlyNodes(parent);

      /* Info. */
      if (xml_isNode(parent,"version")) {
         node = parent->xmlChildrenNode;
         do {
            xmlr_strd(node,"naev",save->version);
            xmlr_strd(node,"data",save->data);
         } while (xml_nextNode(node));
         continue;
      }

      if (xml_isNode(parent,"player")) {
         node = parent->xmlChildrenNode;

         /* Get name. */
         xmlr_attr(parent,"name",save->name);
         /* Parse rest. */
         node = parent->xmlChildrenNode;
         do {
            xml_onlyNodes(node);

            /* Player info. */
            xmlr_strd(node,"location",save->planet);
            xmlr_ulong(node,"credits",save->credits);
            xmlr_ulong(node,"time",save->date);

            /* Ship info. */
            if (xml_isNode(node,"ship")) {
               xmlr_attr(parent,"name",save->shipname);
               xmlr_attr(parent,"model",save->shipmodel);
               continue;
            }
         } while (xml_nextNode(node));
         continue;
      }
   } while (xml_nextNode(parent));

   /* Clean up. */
   xmlFreeDoc(doc);

   return 0;
}


/**
 * @brief Loads or refreshes saved games.
 */
int load_refresh (void)
{
   char **files, buf[PATH_MAX];
   int nfiles, i, len;
   int ok;
   nsave_t *ns;

   if (load_saves != NULL)
      load_free();
   load_saves = array_create( nsave_t );

   /* load the saves */
   files = nfile_readDir( &nfiles, "%ssaves", nfile_basePath() );
   for (i=0; i<nfiles; i++) {
      len = strlen(files[i]);

      /* no save extension */
      if ((len < 5) || strcmp(&files[i][len-3],".ns")) {
         free(files[i]);
         memmove( &files[i], &files[i+1], sizeof(char*) * (nfiles-i-1) );
         nfiles--;
         i--;
      }
   }

   /* Make suref files are none. */
   if (files == NULL)
      return 0;

   /* Allocate and parse. */
   ok = 0;
   for (i=0; i<nfiles; i++) {
      if (!ok)
         ns = &array_grow( &load_saves );
      snprintf( buf, sizeof(buf), "%ssaves/%s", nfile_basePath(), files[i] );
      ok = load_load( ns, buf );
   }

   /* Clean up parser. */
   xmlCleanupParser();

   return 0;
}


/**
 * @brief Frees loaded save stuff.
 */
void load_free (void)
{
   int i;
   nsave_t *ns;

   if (load_saves != NULL) {
      for (i=0; i<array_size(load_saves); i++) {
         ns = &load_saves[i];
         free(ns->path);
         if (ns->name != NULL)
            free(ns->name);

         if (ns->version != NULL)
            free(ns->version);
         if (ns->data != NULL)
            free(ns->data);

         if (ns->planet != NULL)
            free(ns->planet);

         if (ns->shipname != NULL)
            free(ns->shipname);
         if (ns->shipmodel != NULL)
            free(ns->shipmodel);
      }
      array_free( load_saves );
   }
   load_saves = NULL;
}


/**
 * @brief Gets the list of loaded saves.
 */
nsave_t *load_getList( int *n )
{
   if (load_saves == NULL) {
      *n = 0;
      return NULL;
   }

   *n = array_size( load_saves );
   return load_saves;
}

/**
 * @brief Opens the load game menu.
 */
void load_loadGameMenu (void)
{
   unsigned int wid;
   char **names;
   nsave_t *nslist, *ns;
   int i, n;

   /* window */
   wid = window_create( "Load Game", -1, -1, LOAD_WIDTH, LOAD_HEIGHT );
   window_setCancel( wid, load_menu_close );

   /* Load loads. */
   load_refresh();

   /* load the saves */
   nslist = load_getList( &n );
   if (n > 0) {
      names = malloc( sizeof(char*)*n );
      for (i=0; i<n; i++) {
         ns       = &nslist[i];
         names[i] = strdup( ns->name );
      }
   }
   /* case there are no files */
   else {
      names = malloc(sizeof(char*));
      names[0] = strdup("None");
      n     = 1;
   }
   window_addList( wid, 20, -50,
         LOAD_WIDTH-BUTTON_WIDTH-50, LOAD_HEIGHT-110,
         "lstSaves", names, n, 0, NULL );

   /* buttons */
   window_addButton( wid, -20, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnBack", "Back", load_menu_close );
   window_addButton( wid, -20, 30 + BUTTON_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnLoad", "Load", load_menu_load );
   window_addButton( wid, 20, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnDelete", "Del", load_menu_delete );

   /* default action */
   window_setAccept( wid, load_menu_load );
}
/**
 * @brief Closes the load game menu.
 *    @param wdw Window triggering function.
 *    @param str Unused.
 */
static void load_menu_close( unsigned int wdw, char *str )
{
   (void)str;
   window_destroy( wdw );
}
/**
 * @brief Loads a new game.
 *    @param wdw Window triggering function.
 *    @param str Unused.
 */
static void load_menu_load( unsigned int wdw, char *str )
{
   (void)str;
   char *save;
   int wid;
   int pos;
   nsave_t *ns;
   int n;

   wid = window_get( "Load Game" );
   save = toolkit_getList( wid, "lstSaves" );

   if (strcmp(save,"None") == 0)
      return;

   pos = toolkit_getListPos( wid, "lstSaves" );
   ns  = load_getList( &n );

   /* Close menus before loading for proper rendering. */
   load_menu_close(wdw, NULL);
   menu_main_close();

   if (load_game( ns[pos].path )) {
      menu_main();
      load_loadGameMenu();
   }
}
/**
 * @brief Deletes an old game.
 *    @param wdw Window to delete.
 *    @param str Unused.
 */
static void load_menu_delete( unsigned int wdw, char *str )
{
   (void)str;
   char *save;
   int wid, pos;
   nsave_t *ns;
   int n;

   wid = window_get( "Load Game" );
   save = toolkit_getList( wid, "lstSaves" );

   if (strcmp(save,"None") == 0)
      return;

   if (dialogue_YesNo( "Permanently Delete?",
      "Are you sure you want to permanently delete '%s'?", save) == 0)
      return;

   /* Remove it. */
   pos = toolkit_getListPos( wid, "lstSaves" );
   ns  = load_getList( &n );
   remove( ns[pos].path ); /* remove is portable and will call unlink on linux. */

   /* need to reload the menu */
   load_menu_close(wdw, NULL);
   load_loadGameMenu();
}


/**
 * @brief Actually loads a new game based on file.
 *
 *    @param file File that contains the new game.
 *    @return 0 on success.
 */
int load_game( const char* file )
{
   xmlNodePtr node;
   xmlDocPtr doc;
   Planet *pnt;

   /* Make sure it exists. */
   if (!nfile_fileExists(file)) {
      dialogue_alert("Savegame file seems to have been deleted.");
      return -1;
   }

   /* Load the XML. */
   doc   = xmlParseFile(file);
   if (doc == NULL)
      goto err;
   node  = doc->xmlChildrenNode; /* base node */
   if (node == NULL)
      goto err_doc;

   /* Clean up possible stuff that should be cleaned. */
   player_cleanup();
   diff_clear();
   var_cleanup();
   missions_cleanup();
   events_cleanup();

   /* Welcome message - must be before space_init. */
   player_message( "\egWelcome to "APPNAME"!" );
   player_message( "\eg v%s", naev_version(0) );

   /* Now begin to load. */
   diff_load(node); /* Must load first to work properly. */
   pfaction_load(node); /* Must be loaded before player so the messages show up properly. */
   pnt = player_load(node);
   var_load(node);
   missions_loadActive(node);
   events_loadActive(node);
   hook_load(node);
   space_sysLoad(node);

   /* Initialize the economy. */
   economy_init();

   /* Check sanity. */
   event_checkSanity();

   /* Run the load event trigger. */
   events_trigger( EVENT_TRIGGER_LOAD );

   /* Land the player. */
   land( pnt, 1 );

   /* Load the GUI. */
   gui_load( gui_pick() );

   /* Sanitize the GUI. */
   gui_setCargo();

   xmlFreeDoc(doc);
   xmlCleanupParser();

   return 0;

err_doc:
   xmlFreeDoc(doc);
   xmlCleanupParser();
err:
   WARN("Savegame '%s' invalid!", file);
   return -1;
}

