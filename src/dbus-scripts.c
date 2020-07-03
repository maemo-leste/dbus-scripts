
/* dbus-monitor.c  Utility program to monitor messages on the bus
 *
 * Copyright (C) 2003 Philip Blundell <philb@gnu.org>
 * Copyright (C) 2007 Graham Cobb <g+770@cobb.uk.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#define DBUS_CONF_DIR "/etc/dbus-scripts.d"
#define DBUS_RUN_SCRIPTS ""

/* Object definitions for configuration data */
typedef struct _ScriptFile ScriptFile;
typedef struct _ScriptFileClass ScriptFileClass;

struct _ScriptFile {
	GObject parent;
	ScriptFileClass *class;
	gboolean dispose_has_run;
	gchar *script;
	GSList *filterlist;
	gint version;
};

struct _ScriptFileClass {
	GObjectClass parent;
};

static void script_file_class_init(GObjectClass * klass);
static void script_file_init(GTypeInstance * instance, gpointer g_class);

GType script_file_get_type(void) {
	static GType type = 0;
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(ScriptFileClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) script_file_class_init,	/* class_init */
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof(ScriptFile),
			0,	/* n_preallocs */
			(GInstanceInitFunc) script_file_init	/* instance_init */
		};
		type = g_type_register_static(G_TYPE_OBJECT,
					      "ScriptFileType", &info, 0);
	}
	return type;
}

char *msgs[16];
char text[2048];
int nmsgs;
char *msgsv2[16];
char textv2[2048];
int nmsgsv2;
char *cmsgs[32];
int ncmsgs;
int debug;
GSList *script_list;

#if 0
static const char *type_to_name(int message_type) {
	switch (message_type) {
	case DBUS_MESSAGE_TYPE_SIGNAL:
		return "signal";
	case DBUS_MESSAGE_TYPE_METHOD_CALL:
		return "method_call";
	case DBUS_MESSAGE_TYPE_METHOD_RETURN:
		return "method_return";
	case DBUS_MESSAGE_TYPE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}
#endif

static void script_file_call_script(ScriptFile * self, char *args[]) {
	/* Call the scripts with the given arguments */
	/* Note that args[0] will be destroyed (left as NULL) */
	int pid;

	/* Call the script */
	args[0] = g_strdup(self->script);
	if ((pid = fork())) {
		waitpid(pid, NULL, 0);
		g_free(args[0]);
		args[0] = NULL;
	} else {
		/* child */
		execv(self->script, args);
	}
}

static void script_file_call_if_matched(ScriptFile * self, char *args[]) {
	/* Check if this script matches the required arguments and, if so, call it */
	/* Note that args[0] will be destroyed */
	GSList *l;
	int i;

	if (self->version == 2)
		args = cmsgs;

	if (debug > 1)
		printf("Checking %s:\n", self->script);
	for (i = 1, l = self->filterlist; l; i++, l = g_slist_next(l)) {
		if (debug > 1)
			printf(" Argument %d:\n", i);
		if (!args[i])
			return;	/* End of arg list with filters left to test */

		if (debug > 1)
			printf("  filter=%s, arg=%s, match=%d\n",
			       (char *)(l->data), args[i], fnmatch(l->data,
								   args[i], 0));
		if (fnmatch(l->data, args[i], 0))
			return;	/* Does not match */
	}

	if (debug)
		printf("Script %s matches\n", self->script);

	script_file_call_script(self, args);
}

static void call_scripts(GSList * script_list, char *args[]) {
	g_slist_foreach(script_list, (GFunc) script_file_call_if_matched, args);
}

static void print_iter(DBusMessageIter * iter, dbus_bool_t literal, int depth) {
	do {
		int type = dbus_message_iter_get_arg_type(iter);
		const char *str;
		dbus_uint32_t uint32;
		dbus_int32_t int32;
		dbus_bool_t boolean;
#if 0
		double d;
		unsigned char byte;
#endif

		if (type == DBUS_TYPE_INVALID)
			break;

		switch (type) {
		case DBUS_TYPE_STRING:
			dbus_message_iter_get_basic(iter, &str);
			strncpy(msgs[nmsgs], str, 127);
			nmsgs++;
			break;
#if 1
		case DBUS_TYPE_INT32:
			dbus_message_iter_get_basic(iter, &int32);
			sprintf(msgsv2[nmsgsv2], "%d", int32);
			nmsgsv2++;
			break;

		case DBUS_TYPE_UINT32:
			dbus_message_iter_get_basic(iter, &uint32);
			sprintf(msgsv2[nmsgsv2], "%u", uint32);
			nmsgsv2++;
			break;
		case DBUS_TYPE_BOOLEAN:
			dbus_message_iter_get_basic(iter, &boolean);
			sprintf(msgsv2[nmsgsv2], "%s",
				boolean ? "true" : "false");
			nmsgsv2++;
			break;

#endif

#if 0
		case DBUS_TYPE_DOUBLE:
			dbus_message_iter_get_basic(iter, &d);
			printf("double %g\n", d);
			break;

		case DBUS_TYPE_BYTE:
			dbus_message_iter_get_basic(iter, &byte);
			printf("byte %d\n", byte);
			break;

		case DBUS_TYPE_VARIANT:
			{
				DBusMessageIter subiter;

				dbus_message_iter_recurse(iter, &subiter);

				printf("variant:");
				print_iter(&subiter, literal, depth);
				break;
			}
		case DBUS_TYPE_ARRAY:
			{
				int current_type;
				DBusMessageIter subiter;

				dbus_message_iter_recurse(iter, &subiter);

				printf("[");
				while ((current_type =
					dbus_message_iter_get_arg_type
					(&subiter)) != DBUS_TYPE_INVALID) {
					print_iter(&subiter, literal, depth);
					dbus_message_iter_next(&subiter);
					if (dbus_message_iter_get_arg_type
					    (&subiter) != DBUS_TYPE_INVALID)
						printf(",");
				}
				printf("]");
				break;
			}
		case DBUS_TYPE_DICT_ENTRY:
			{
				DBusMessageIter subiter;

				dbus_message_iter_recurse(iter, &subiter);

				printf("{");
				print_iter(&subiter, literal, depth);
				dbus_message_iter_next(&subiter);
				print_iter(&subiter, literal, depth);
				printf("}");
				break;
			}
#endif
		default:
			break;
		}

	} while (dbus_message_iter_next(iter));
}

void print_message(DBusMessage * message, dbus_bool_t literal) {
	DBusMessageIter iter;
	const char *sender;
	const char *path;
	const char *destination;
	int message_type;
	int i;

	message_type = dbus_message_get_type(message);
	sender = dbus_message_get_sender(message);
	path = dbus_message_get_path(message);
	destination = dbus_message_get_destination(message);

	for (i = 0; i < 16; i++)
		msgs[i] = text + 128 * i;
	for (i = 0; i < 16; i++)
		msgsv2[i] = textv2 + 128 * i;
	nmsgs = 1;
	nmsgsv2 = 0;
	if (!literal) {
		strncpy(msgs[nmsgs], sender ? sender : "null", 127);
		nmsgs++;
		strncpy(msgs[nmsgs], destination ? destination : "null", 127);
		nmsgs++;
		strncpy(msgsv2[nmsgsv2], path ? path : "null", 127);
		nmsgsv2++;

		switch (message_type) {
		case DBUS_MESSAGE_TYPE_METHOD_CALL:
		case DBUS_MESSAGE_TYPE_SIGNAL:
			strncpy(msgs[nmsgs],
				dbus_message_get_interface(message), 127);
			nmsgs++;
			strncpy(msgs[nmsgs], dbus_message_get_member(message),
				127);
			nmsgs++;
			break;

		case DBUS_MESSAGE_TYPE_METHOD_RETURN:
			break;

		case DBUS_MESSAGE_TYPE_ERROR:
			strncpy(msgs[nmsgs],
				dbus_message_get_error_name(message), 127);
			nmsgs++;
			break;

		default:
			break;
		}
	}

	dbus_message_iter_init(message, &iter);
	print_iter(&iter, literal, 1);

	if (debug) {
		printf("=================================\n");
		for (i = 1; i < nmsgs; i++)
			printf("Arg %d: %s\n", i, msgs[i]);
	}

	msgs[nmsgs] = NULL;
	cmsgs[1] = msgs[1];
	cmsgs[2] = msgsv2[0];
	for (i = 2; i < nmsgs; i++)
		cmsgs[i + 1] = msgs[i];
	for (i = 1; i < nmsgsv2; i++)
		cmsgs[i + nmsgs] = msgsv2[i];
	ncmsgs = nmsgs + nmsgsv2;
	cmsgs[ncmsgs] = NULL;

	call_scripts(script_list, msgs);
}

static DBusHandlerResult
filter_func(DBusConnection * connection,
	    DBusMessage * message, void *user_data) {
	print_message(message, FALSE);

	if (dbus_message_is_signal(message,
				   DBUS_INTERFACE_LOCAL, "Disconnected"))
		exit(0);

	/* Conceptually we want this to be
	 * DBUS_HANDLER_RESULT_NOT_YET_HANDLED, but this raises
	 * some problems.  See bug 1719.
	 */
	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Script File class functions */
static void script_file_dispose(GObject * obj) {
	ScriptFile *self = (ScriptFile *) obj;

	if (self->dispose_has_run)
		return;
	self->dispose_has_run = TRUE;

	g_free(self->script);
	g_slist_foreach(self->filterlist, (GFunc) g_free, NULL);
}
static void script_file_finalize(GObject * obj) {
//  ScriptFile *self = (ScriptFile *)obj;
}
static void script_file_class_init(GObjectClass * class) {
	class->dispose = script_file_dispose;
	class->finalize = script_file_finalize;
}
static void script_file_init(GTypeInstance * instance, gpointer g_class) {
	ScriptFile *self = (ScriptFile *) instance;
	self->class = (ScriptFileClass *) g_class;
	self->dispose_has_run = FALSE;
	self->script = NULL;
	self->filterlist = NULL;
}

GSList *parse_conf_file(char *fname, GSList * list) {
	/* Parse a config file and add any scripts to the list */

	gint version;
	FILE *fp;
	char buf[4096];

	if (debug)
		printf("Parsing conf file %s...\n", fname);

	version = 1;

	fp = fopen(fname, "r");
	if (!fp) {
		perror(fname);
		exit(1);
	}

	/* read file, line by line */
	while (fgets(buf, sizeof(buf), fp)) {
#define WHITE " \t\n\r"
		char *ptr = buf;
		char *token;

		// if (debug) printf("Line: %s\n", ptr);

		/* Skip any leading whitespace */
		ptr += strspn(ptr, WHITE);

		if (ptr[0] == 0)
			continue;
		if (ptr[0] == '#') {
			if (!strncmp(ptr + 1, "DBUSV2.", 7))
				version = 2;
			if (debug) {
				printf("Here v=%i %s\n", version, ptr + 1);
			}
			continue;
		}

		/* Find filename */
		token = strtok(ptr, WHITE);
		if (!token)
			continue;

		ScriptFile *obj = g_object_new(script_file_get_type(), NULL);
		list = g_slist_append(list, obj);

		obj->script = g_strdup(token);
		obj->version = version;

		while ((token = strtok(NULL, WHITE))) {
			obj->filterlist =
			    g_slist_append(obj->filterlist, g_strdup(token));
		}
	}
	fclose(fp);

	return list;
}

static GSList *read_conf_dir(char *confdir) {
	/* Find all the conf files in the conf directory and parse them */
	GSList *list = NULL;
	struct dirent *f;
	DIR *dir = opendir(confdir);

	while ((f = readdir(dir))) {
		gchar *s = g_strconcat(confdir, "/", f->d_name, NULL);
		list = parse_conf_file(s, list);
		g_free(s);
	}

	closedir(dir);

	return list;
}

static void print_script_info(ScriptFile * obj) {
	GSList *l;
	int i;

	printf("Script file %s: ", obj->script);
	for (i = 1, l = obj->filterlist; l; i++, l = g_slist_next(l)) {
		printf("arg %d = %s, ", i, (char *)l->data);
	}
	printf("\n");
}

static void usage(char *name, int ecode) {
	fprintf(stderr,
		"Usage: %s [--debug] [--system | --session | --system-and-session] [--confdir <directory>] [watch expressions]\n",
		name);
	exit(ecode);
}

int main(int argc, char *argv[]) {
	DBusConnection *connection[2];
	DBusError error;
	DBusBusType type = DBUS_BUS_SESSION;
	GMainLoop *loop;
	int both = 0;
	int i = 0, j = 0, numFilters = 0;
	char **filters = NULL;
	char *confdir = DBUS_CONF_DIR;

	debug = 0;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "--system"))
			type = DBUS_BUS_SYSTEM;
		else if (!strcmp(arg, "--session"))
			type = DBUS_BUS_SESSION;
		else if (!strcmp(arg, "--system-and-session"))
			both = 1;
		else if (!strcmp(arg, "--debug"))
			debug++;
		else if (!strcmp(arg, "--confdir")) {
			i++;
			confdir = argv[i];
		} else if (!strcmp(arg, "--help"))
			usage(argv[0], 0);
		else if (!strcmp(arg, "--"))
			continue;
		else if (arg[0] == '-')
			usage(argv[0], 1);
		else {
			numFilters++;
			filters =
			    (char **)realloc(filters,
					     numFilters * sizeof(char *));
			filters[j] =
			    (char *)malloc((strlen(arg) + 1) * sizeof(char *));
			snprintf(filters[j], strlen(arg) + 1, "%s", arg);
			j++;
		}
	}

	script_list = read_conf_dir(confdir);
	if (debug)
		g_slist_foreach(script_list, (GFunc) print_script_info, NULL);

	loop = g_main_loop_new(NULL, FALSE);

	dbus_error_init(&error);
	if (both) {
		dbus_error_init(&error);
		connection[0] = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
		if (connection[0] == NULL) {
			fprintf(stderr,
				"Failed to open connection to %s message bus: %s\n",
				"system", error.message);
			dbus_error_free(&error);
			exit(1);
		}
		connection[1] = dbus_bus_get(DBUS_BUS_SESSION, &error);
		if (connection[1] == NULL) {
			fprintf(stderr,
				"Failed to open connection to %s message bus: %s\n",
				"session", error.message);
			dbus_error_free(&error);
			exit(1);
		}

	} else {
		dbus_error_init(&error);
		connection[0] = dbus_bus_get(type, &error);
		if (connection[0] == NULL) {
			fprintf(stderr,
				"Failed to open connection to %s message bus: %s\n",
				(type ==
				 DBUS_BUS_SYSTEM) ? "system" : "session",
				error.message);
			dbus_error_free(&error);
			exit(1);
		}
	}

	for (int k = 0; k < both + 1; k++) {
		dbus_connection_setup_with_g_main(connection[k], NULL);
	}

	for (int k = 0; k < both + 1; k++) {
		if (numFilters) {
			for (i = 0; i < j; i++) {
				dbus_bus_add_match(connection[k], filters[i],
						   &error);
				if (dbus_error_is_set(&error)) {
					fprintf(stderr,
						"Failed to setup match \"%s\": %s\n",
						filters[i], error.message);
					dbus_error_free(&error);
					exit(1);
				}
                if (j == both)
                    free(filters[i]);
			}
		} else {
			dbus_bus_add_match(connection[k], "type='signal'",
					   &error);
			if (dbus_error_is_set(&error))
				goto lose;
			dbus_bus_add_match(connection[k], "type='method_call'",
					   &error);
			if (dbus_error_is_set(&error))
				goto lose;
			dbus_bus_add_match(connection[k],
					   "type='method_return'", &error);
			if (dbus_error_is_set(&error))
				goto lose;
			dbus_bus_add_match(connection[k], "type='error'",
					   &error);
			if (dbus_error_is_set(&error))
				goto lose;
		}

		if (!dbus_connection_add_filter
		    (connection[k], filter_func, NULL, NULL)) {
			fprintf(stderr, "Couldn't add filter!\n");
			exit(1);
		}
	}

	g_main_loop_run(loop);

	exit(0);
 lose:
	fprintf(stderr, "Error: %s\n", error.message);
	exit(1);
}
