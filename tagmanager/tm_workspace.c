/*
*
*   Copyright (c) 2001-2002, Biswapesh Chattopadhyay
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License.
*
*/

#include "general.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_GLOB_H
# include <glob.h>
#endif
// handling of P_tmpdir, should be something like /tmp, take the root directory under Win32,
// and assume /tmp on non-Win32 systems where P_tmpdir is not set
#ifndef P_tmpdir
# ifdef G_OS_WIN32
#  define P_tmpdir "\\"
# else
#  define P_tmpdir "/tmp"
# endif
#endif

#include "tm_tag.h"
#include "tm_workspace.h"
#include "tm_project.h"


static TMWorkspace *theWorkspace = NULL;
guint workspace_class_id = 0;

static gboolean tm_create_workspace(void)
{
#ifdef G_OS_WIN32
	char *file_name = g_strdup_printf("%s_%s_%ld.%d", P_tmpdir, PACKAGE, time(NULL), getpid());
#else
	char *file_name = g_strdup_printf("%s/%s_%ld.%d", P_tmpdir, PACKAGE, time(NULL), getpid());
#endif

	workspace_class_id = tm_work_object_register(tm_workspace_free, tm_workspace_update
		  , tm_workspace_find_object);
	theWorkspace = g_new(TMWorkspace, 1);
	if (FALSE == tm_work_object_init(TM_WORK_OBJECT(theWorkspace),
		  workspace_class_id, file_name, TRUE))
	{
		g_free(file_name);
		g_free(theWorkspace);
		theWorkspace = NULL;
		g_warning("Failed to initialize workspace");
		return FALSE;
	}

	g_free(file_name);
	theWorkspace->global_tags = NULL;
	theWorkspace->work_objects = NULL;
	return TRUE;
}

void tm_workspace_free(gpointer workspace)
{
	guint i;

	if (workspace != theWorkspace)
		return;

#ifdef TM_DEBUG
	g_message("Workspace destroyed");
#endif

	if (theWorkspace)
	{
		if (theWorkspace->work_objects)
		{
			for (i=0; i < theWorkspace->work_objects->len; ++i)
				tm_work_object_free(theWorkspace->work_objects->pdata[i]);
			g_ptr_array_free(theWorkspace->work_objects, TRUE);
		}
		if (theWorkspace->global_tags)
		{
			for (i=0; i < theWorkspace->global_tags->len; ++i)
				tm_tag_free(theWorkspace->global_tags->pdata[i]);
			g_ptr_array_free(theWorkspace->global_tags, TRUE);
		}
		unlink(theWorkspace->work_object.file_name);
		tm_work_object_destroy(TM_WORK_OBJECT(theWorkspace));
		g_free(theWorkspace);
		theWorkspace = NULL;
	}
}

const TMWorkspace *tm_get_workspace(void)
{
	if (NULL == theWorkspace)
		tm_create_workspace();
	return theWorkspace;
}

gboolean tm_workspace_add_object(TMWorkObject *work_object)
{
	if (NULL == theWorkspace)
		tm_create_workspace();
	if (NULL == theWorkspace->work_objects)
		theWorkspace->work_objects = g_ptr_array_new();
	g_ptr_array_add(theWorkspace->work_objects, work_object);
	work_object->parent = TM_WORK_OBJECT(theWorkspace);
	return TRUE;
}

gboolean tm_workspace_remove_object(TMWorkObject *w, gboolean free)
{
	guint i;
	if ((NULL == theWorkspace) || (NULL == theWorkspace->work_objects)
		  || (NULL == w))
		return FALSE;
	for (i=0; i < theWorkspace->work_objects->len; ++i)
	{
		if (theWorkspace->work_objects->pdata[i] == w)
		{
			if (free)
				tm_work_object_free(w);
			g_ptr_array_remove_index_fast(theWorkspace->work_objects, i);
			tm_workspace_update(TM_WORK_OBJECT(theWorkspace), TRUE, FALSE, FALSE);
			return TRUE;
		}
	}
	return FALSE;
}

static TMTagAttrType global_tags_sort_attrs[] =
{
	tm_tag_attr_name_t, tm_tag_attr_scope_t,
	tm_tag_attr_type_t, tm_tag_attr_arglist_t, 0
};

gboolean tm_workspace_load_global_tags(const char *tags_file, gint mode)
{
	FILE *fp;
	TMTag *tag;

	if (NULL == (fp = fopen(tags_file, "r")))
		return FALSE;
	if (NULL == theWorkspace)
		tm_create_workspace();
	if (NULL == theWorkspace->global_tags)
		theWorkspace->global_tags = g_ptr_array_new();
	while (NULL != (tag = tm_tag_new_from_file(NULL, fp, mode)))
		g_ptr_array_add(theWorkspace->global_tags, tag);
	fclose(fp);

	// resort the whole array, because tm_tags_find expects a sorted array and it is not sorted
	// when global.tags, php.tags and latex.tags are loaded at the same time
	tm_tags_sort(theWorkspace->global_tags, global_tags_sort_attrs, TRUE);

	return TRUE;
}

static guint tm_file_inode_hash(gconstpointer key)
{
	struct stat file_stat;
	const char *filename = (const char*)key;
	if (stat(filename, &file_stat) == 0)
	{
#ifdef TM_DEBUG
		g_message ("Hash for '%s' is '%d'\n", filename, file_stat.st_ino);
#endif
		return g_direct_hash (GUINT_TO_POINTER (file_stat.st_ino));
	} else {
		return 0;
	}
}

static void tm_move_entries_to_g_list(gpointer key, gpointer value, gpointer user_data)
{
	GList **pp_list = (GList**)user_data;

	if (user_data == NULL)
		return;

	*pp_list = g_list_prepend(*pp_list, value);
}

static void write_includes_file(FILE *fp, GList *includes_files)
{
	GList *node;

	node = includes_files;
	while (node)
	{
		char *str = g_strdup_printf("#include \"%s\"\n", (char*)node->data);
		int str_len = strlen(str);

		fwrite(str, str_len, 1, fp);
		free(str);
		node = g_list_next (node);
	}
}


static void append_to_temp_file(FILE *fp, GList *file_list)
{
	GList *node;

	node = file_list;
	while (node)
	{
		const char *fname = node->data;
		char *contents;
		size_t length;
		GError *err = NULL;

		if (! g_file_get_contents(fname, &contents, &length, &err))
		{
			fprintf(stderr, "Unable to read file: %s\n", err->message);
			g_error_free(err);
		}
		else
		{
			fwrite(contents, length, 1, fp);
			fwrite("\n", 1, 1, fp);	// in case file doesn't end in newline (e.g. windows).
			g_free(contents);
		}
		node = g_list_next (node);
	}
}

static gint get_global_tag_type_mask(gint lang)
{
	switch (lang)
	{
		case 0:
		case 1:
			// C/C++
			return tm_tag_class_t | tm_tag_typedef_t | tm_tag_enum_t | tm_tag_enumerator_t |
				tm_tag_prototype_t |
				tm_tag_function_t | tm_tag_method_t |	// for inline functions
				tm_tag_macro_t | tm_tag_macro_with_arg_t;
		default:
			return tm_tag_max_t;
	}
}

gboolean tm_workspace_create_global_tags(const char *pre_process, const char **includes
  , int includes_count, const char *tags_file, int lang)
{
#ifdef HAVE_GLOB_H
	glob_t globbuf;
	size_t idx_glob;
#endif
	int idx_inc;
	char *command;
	guint i;
	FILE *fp;
	TMWorkObject *source_file;
	GPtrArray *tags_array;
	GHashTable *includes_files_hash;
	GList *includes_files = NULL;
#ifdef G_OS_WIN32
	char *temp_file = g_strdup_printf("%s_%d_%ld_1.cpp", P_tmpdir, getpid(), time(NULL));
	char *temp_file2 = g_strdup_printf("%s_%d_%ld_2.cpp", P_tmpdir, getpid(), time(NULL));
#else
	char *temp_file = g_strdup_printf("%s/%d_%ld_1.cpp", P_tmpdir, getpid(), time(NULL));
	char *temp_file2 = g_strdup_printf("%s/%d_%ld_2.cpp", P_tmpdir, getpid(), time(NULL));
#endif

	if (NULL == (fp = fopen(temp_file, "w")))
		return FALSE;

	includes_files_hash = g_hash_table_new_full (tm_file_inode_hash,
												 g_direct_equal,
												 NULL, g_free);

#ifdef HAVE_GLOB_H
	globbuf.gl_offs = 0;

	if (includes[0][0] == '"')	// leading \" char for glob matching
	for(idx_inc = 0; idx_inc < includes_count; idx_inc++)
	{
 		int dirty_len = strlen(includes[idx_inc]);
		char *clean_path = malloc(dirty_len - 1);
		strncpy(clean_path, includes[idx_inc] + 1, dirty_len - 1);
		clean_path[dirty_len - 2] = 0;

#ifdef TM_DEBUG
		g_message ("[o][%s]\n", clean_path);
#endif
		glob(clean_path, 0, NULL, &globbuf);

#ifdef TM_DEBUG
		g_message ("matches: %d\n", globbuf.gl_pathc);
#endif

		for(idx_glob = 0; idx_glob < globbuf.gl_pathc; idx_glob++)
		{
#ifdef TM_DEBUG
			g_message (">>> %s\n", globbuf.gl_pathv[idx_glob]);
#endif
			if (!g_hash_table_lookup(includes_files_hash,
									globbuf.gl_pathv[idx_glob]))
			{
				char* file_name_copy = strdup(globbuf.gl_pathv[idx_glob]);
				g_hash_table_insert(includes_files_hash, file_name_copy,
									file_name_copy);
#ifdef TM_DEBUG
				g_message ("Added ...\n");
#endif
			}
		}
		globfree(&globbuf);
		free(clean_path);
  	}
  	else
#endif
	// no glob support or globbing not wanted
	for(idx_inc = 0; idx_inc < includes_count; idx_inc++)
	{
		if (!g_hash_table_lookup(includes_files_hash,
									includes[idx_inc]))
		{
			char* file_name_copy = strdup(includes[idx_inc]);
			g_hash_table_insert(includes_files_hash, file_name_copy,
								file_name_copy);
		}
  	}

	/* Checks for duplicate file entries which would case trouble */
	g_hash_table_foreach(includes_files_hash, tm_move_entries_to_g_list,
						 &includes_files);

	includes_files = g_list_reverse (includes_files);

#ifdef TM_DEBUG
	g_message ("writing out files to %s\n", temp_file);
#endif
	if (pre_process != NULL)
		write_includes_file(fp, includes_files);
	else
		append_to_temp_file(fp, includes_files);

	g_list_free (includes_files);
	g_hash_table_destroy(includes_files_hash);
	includes_files_hash = NULL;
	includes_files = NULL;
	fclose(fp);

	/* FIXME: The following grep command removes the lines
	 * G_BEGIN_DECLS and G_END_DECLS from the header files. The reason is
	 * that in tagmanager, the files are not correctly parsed and the typedefs
	 * following these lines are incorrectly parsed. The real fix should,
	 * of course be in tagmanager (c) parser. This is just a temporary fix.
	 */
	if (pre_process != NULL)
	{
		command = g_strdup_printf("%s %s | grep -v -E '^\\s*(G_BEGIN_DECLS|G_END_DECLS)\\s*$' > %s",
							  pre_process, temp_file, temp_file2);
#ifdef TM_DEBUG
		g_message("Executing: %s", command);
#endif
		system(command);
		g_free(command);
		unlink(temp_file);
		g_free(temp_file);
	}
	else
	{
		// no pre-processing needed, so temp_file2 = temp_file
		g_free(temp_file2);
		temp_file2 = temp_file;
		temp_file = NULL;
	}
	source_file = tm_source_file_new(temp_file2, TRUE, tm_source_file_get_lang_name(lang));
	if (NULL == source_file)
	{
		unlink(temp_file2);
		return FALSE;
	}
	unlink(temp_file2);
	g_free(temp_file2);
	if ((NULL == source_file->tags_array) || (0 == source_file->tags_array->len))
	{
		tm_source_file_free(source_file);
		return FALSE;
	}
	tags_array = tm_tags_extract(source_file->tags_array, get_global_tag_type_mask(lang));
	if ((NULL == tags_array) || (0 == tags_array->len))
	{
		if (tags_array)
			g_ptr_array_free(tags_array, TRUE);
		tm_source_file_free(source_file);
		return FALSE;
	}
	if (FALSE == tm_tags_sort(tags_array, global_tags_sort_attrs, TRUE))
	{
		tm_source_file_free(source_file);
		return FALSE;
	}
	if (NULL == (fp = fopen(tags_file, "w")))
	{
		tm_source_file_free(source_file);
		return FALSE;
	}
	for (i = 0; i < tags_array->len; ++i)
	{
		tm_tag_write(TM_TAG(tags_array->pdata[i]), fp, tm_tag_attr_type_t
		  | tm_tag_attr_scope_t | tm_tag_attr_arglist_t | tm_tag_attr_vartype_t
		  | tm_tag_attr_pointer_t);
	}
	fclose(fp);
	tm_source_file_free(source_file);
	g_ptr_array_free(tags_array, TRUE);
	return TRUE;
}

TMWorkObject *tm_workspace_find_object(TMWorkObject *work_object, const char *file_name
  , gboolean name_only)
{
	TMWorkObject *w = NULL;
	guint i;

	if (work_object != TM_WORK_OBJECT(theWorkspace))
		return NULL;
	if ((NULL == theWorkspace) || (NULL == theWorkspace->work_objects)
		|| (0 == theWorkspace->work_objects->len))
		return NULL;
	for (i = 0; i < theWorkspace->work_objects->len; ++i)
	{
		if (NULL != (w = tm_work_object_find(TM_WORK_OBJECT(theWorkspace->work_objects->pdata[i])
			  , file_name, name_only)))
			return w;
	}
	return NULL;
}

void tm_workspace_recreate_tags_array(void)
{
	guint i, j;
	TMWorkObject *w;
	TMTagAttrType sort_attrs[] = { tm_tag_attr_name_t, tm_tag_attr_file_t
		, tm_tag_attr_scope_t, tm_tag_attr_type_t, tm_tag_attr_arglist_t, 0};

#ifdef TM_DEBUG
	g_message("Recreating workspace tags array");
#endif

	if ((NULL == theWorkspace) || (NULL == theWorkspace->work_objects))
		return;
	if (NULL != theWorkspace->work_object.tags_array)
		g_ptr_array_set_size(theWorkspace->work_object.tags_array, 0);
	else
		theWorkspace->work_object.tags_array = g_ptr_array_new();

#ifdef TM_DEBUG
	g_message("Total %d objects", theWorkspace->work_objects->len);
#endif
	for (i=0; i < theWorkspace->work_objects->len; ++i)
	{
		w = TM_WORK_OBJECT(theWorkspace->work_objects->pdata[i]);
#ifdef TM_DEBUG
		g_message("Adding tags of %s", w->file_name);
#endif
		if ((NULL != w) && (NULL != w->tags_array) && (w->tags_array->len > 0))
		{
			for (j = 0; j < w->tags_array->len; ++j)
			{
				g_ptr_array_add(theWorkspace->work_object.tags_array,
					  w->tags_array->pdata[j]);
			}
		}
	}
#ifdef TM_DEBUG
	g_message("Total: %d tags", theWorkspace->work_object.tags_array->len);
#endif
	tm_tags_sort(theWorkspace->work_object.tags_array, sort_attrs, TRUE);
}

gboolean tm_workspace_update(TMWorkObject *workspace, gboolean force
  , gboolean recurse, gboolean __unused__ update_parent)
{
	guint i;
	gboolean update_tags = force;

#ifdef TM_DEBUG
	g_message("Updating workspace");
#endif

	if (workspace != TM_WORK_OBJECT(theWorkspace))
		return FALSE;
	if (NULL == theWorkspace)
		return TRUE;
	if ((recurse) && (theWorkspace->work_objects))
	{
		for (i=0; i < theWorkspace->work_objects->len; ++i)
		{
			if (TRUE == tm_work_object_update(TM_WORK_OBJECT(
				  theWorkspace->work_objects->pdata[i]), FALSE, TRUE, FALSE))
				update_tags = TRUE;
		}
	}
	if (update_tags)
		tm_workspace_recreate_tags_array();
	workspace->analyze_time = time(NULL);
	return update_tags;
}

void tm_workspace_dump(void)
{
	if (theWorkspace)
	{
#ifdef TM_DEBUG
		g_message("Dumping TagManager workspace tree..");
#endif
		tm_work_object_dump(TM_WORK_OBJECT(theWorkspace));
		if (theWorkspace->work_objects)
		{
			guint i;
			for (i=0; i < theWorkspace->work_objects->len; ++i)
			{
				if (IS_TM_PROJECT(TM_WORK_OBJECT(theWorkspace->work_objects->pdata[i])))
					tm_project_dump(TM_PROJECT(theWorkspace->work_objects->pdata[i]));
				else
					tm_work_object_dump(TM_WORK_OBJECT(theWorkspace->work_objects->pdata[i]));
			}
		}
	}
}

const GPtrArray *tm_workspace_find(const char *name, int type, TMTagAttrType *attrs
 , gboolean partial, langType lang)
{
	static GPtrArray *tags = NULL;
	TMTag **matches[2];
	int len, tagCount[2]={0,0}, tagIter;
	gint tags_lang;

	if ((!theWorkspace) || (!name))
		return NULL;
	len = strlen(name);
	if (!len)
		return NULL;
	if (tags)
		g_ptr_array_set_size(tags, 0);
	else
		tags = g_ptr_array_new();

	matches[0] = tm_tags_find(theWorkspace->work_object.tags_array, name, partial, &tagCount[0]);
	matches[1] = tm_tags_find(theWorkspace->global_tags, name, partial, &tagCount[1]);

	// file tags
	if (matches[0] && *matches[0])
	{
		// tag->atts.file.lang contains the line of the tag and
		// tags->atts.entry.file->lang contains the language
		tags_lang = (*matches[0])->atts.entry.file->lang;

		for (tagIter=0;tagIter<tagCount[0];++tagIter)
		{
			if ((type & (*matches[0])->type) && (lang == -1 || tags_lang == lang))
				g_ptr_array_add(tags, *matches[0]);
			if (partial)
			{
				if (0 != strncmp((*matches[0])->name, name, len))
					break;
			}
			else
			{
				if (0 != strcmp((*matches[0])->name, name))
					break;
			}
			++ matches[0];
		}
	}

	// global tags
	if (matches[1] && *matches[1])
	{
		int tags_lang_alt = 0;
		// tag->atts.file.lang contains the language and
		// tags->atts.entry.file is NULL
		tags_lang = (*matches[1])->atts.file.lang;
		// tags_lang_alt is used to load C global tags only once for C and C++
		// lang = 1 is C++, lang = 0 is C
		// if we have lang 0, than accept also lang 1 for C++
		if (tags_lang == 0)	// C or C++
			tags_lang_alt = 1;
		else
			tags_lang_alt = tags_lang; // otherwise just ignore it

		for (tagIter=0;tagIter<tagCount[1];++tagIter)
		{
			if ((type & (*matches[1])->type) && (lang == -1 ||
				tags_lang == lang || tags_lang_alt == lang))
				g_ptr_array_add(tags, *matches[1]);

			if (partial)
			{
				if (0 != strncmp((*matches[1])->name, name, len))
					break;
			}
			else
			{
				if (0 != strcmp((*matches[1])->name, name))
					break;
			}
			++ matches[1];
		}
	}

	if (attrs)
		tm_tags_sort(tags, attrs, TRUE);
	return tags;
}

static gboolean match_langs(gint lang, const TMTag *tag)
{
	if (tag->atts.entry.file)
	{	// workspace tag
		if (lang == tag->atts.entry.file->lang)
			return TRUE;
	}
	else
	{	// global tag
		if (lang == tag->atts.file.lang)
			return TRUE;
	}
	return FALSE;
}

/* scope can be NULL.
 * lang can be -1 */
static int
fill_find_tags_array (GPtrArray *dst, const GPtrArray *src,
					  const char *name, const char *scope, int type, gboolean partial,
					  gint lang, gboolean first)
{
	TMTag **match;
	int tagIter, count;

	if ((!src) || (!dst) || (!name) || (!*name))
		return 0;

	match = tm_tags_find (src, name, partial, &count);
	if (count && match && *match)
	{
		for (tagIter = 0; tagIter < count; ++tagIter)
		{
			if (! scope || (match[tagIter]->atts.entry.scope &&
				0 == strcmp(match[tagIter]->atts.entry.scope, scope)))
			{
				if (type & match[tagIter]->type)
				if (lang == -1 || match_langs(lang, match[tagIter]))
				{
					g_ptr_array_add (dst, match[tagIter]);
					if (first)
						break;
				}
			}
		}
	}
	return dst->len;
}


// adapted from tm_workspace_find, Anjuta 2.02
const GPtrArray *
tm_workspace_find_scoped (const char *name, const char *scope, gint type,
		TMTagAttrType *attrs, gboolean partial, langType lang, gboolean global_search)
{
	static GPtrArray *tags = NULL;

	if ((!theWorkspace))
		return NULL;

	if (tags)
		g_ptr_array_set_size (tags, 0);
	else
		tags = g_ptr_array_new ();

	fill_find_tags_array (tags, theWorkspace->work_object.tags_array,
						  name, scope, type, partial, lang, FALSE);
	if (global_search)
	{
		// for a scoped tag, I think we always want the same language
		fill_find_tags_array (tags, theWorkspace->global_tags,
							  name, scope, type, partial, lang, FALSE);
	}
	if (attrs)
		tm_tags_sort (tags, attrs, TRUE);
	return tags;
}


const TMTag *
tm_get_current_function (GPtrArray * file_tags, const gulong line)
{
	GPtrArray *const local = tm_tags_extract (file_tags, tm_tag_function_t);
	if (local && local->len)
	{
		guint i;
		TMTag *tag, *function_tag = NULL;
		gulong function_line = 0;
		glong delta;

		for (i = 0; (i < local->len); ++i)
		{
			tag = TM_TAG (local->pdata[i]);
			delta = line - tag->atts.entry.line;
			if (delta >= 0 && (gulong)delta < line - function_line)
			{
				function_tag = tag;
				function_line = tag->atts.entry.line;
			}
		}
		g_ptr_array_free (local, TRUE);
		return function_tag;
	}
	return NULL;
};


const GPtrArray *tm_workspace_get_parents(const gchar *name)
{
	static TMTagAttrType type[] = { tm_tag_attr_name_t, tm_tag_attr_none_t };
	static GPtrArray *parents = NULL;
	const GPtrArray *matches;
	guint i = 0;
	guint j;
	gchar **klasses;
	gchar **klass;
	TMTag *tag;

	g_return_val_if_fail(name && isalpha(*name),NULL);

	if (NULL == parents)
		parents = g_ptr_array_new();
	else
		g_ptr_array_set_size(parents, 0);
	matches = tm_workspace_find(name, tm_tag_class_t, type, FALSE, -1);
	if ((NULL == matches) || (0 == matches->len))
		return NULL;
	g_ptr_array_add(parents, matches->pdata[0]);
	while (i < parents->len)
	{
		tag = TM_TAG(parents->pdata[i]);
		if ((NULL != tag->atts.entry.inheritance) && (isalpha(tag->atts.entry.inheritance[0])))
		{
			klasses = g_strsplit(tag->atts.entry.inheritance, ",", 10);
			for (klass = klasses; (NULL != *klass); ++ klass)
			{
				for (j=0; j < parents->len; ++j)
				{
					if (0 == strcmp(*klass, TM_TAG(parents->pdata[j])->name))
						break;
				}
				if (parents->len == j)
				{
					matches = tm_workspace_find(*klass, tm_tag_class_t, type, FALSE, -1);
					if ((NULL != matches) && (0 < matches->len))
						g_ptr_array_add(parents, matches->pdata[0]);
				}
			}
			g_strfreev(klasses);
		}
		++ i;
	}
	return parents;
}
