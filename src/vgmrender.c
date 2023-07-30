#include "vgmrender.h"
#include "common.h"

#ifndef DLAR_SCRIPT
	#define DLAR_SCRIPT "./download_and_render.sh"
#endif

#include <string.h>
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <linux/limits.h> // PATH_MAX
#include <unistd.h>

#include <json.h>

#include <concord/log.h>

// data that is passed to the background render thread
struct renderthreadData {
	const char* workDir;
	const char* renderCmd;
	const u64snowflake channelId;
};

// a thread that runs in the background to render some audio
// receives struct renderthreadData
// on success, puts result into finishedRenders linked-list
void* render_thread (void* argsRaw) {
	struct renderthreadData* args = (struct renderthreadData*) argsRaw;

	// something seems to be flawed with the way the data is passed to the render thread, seems to be made invalid
	// worked around here by instantly making copies of the values

	size_t len = strlen (args->workDir);
	char* copyDir = malloc (++len);
	strncpy (copyDir, args->workDir, len);

	len = strlen (args->renderCmd);
	char* copyCmd = malloc (++len);
	strncpy (copyCmd, args->renderCmd, len);

	u64snowflake channelId = args->channelId;

	char* outFile = malloc (PATH_MAX);
	snprintf (outFile, PATH_MAX,
		"%s/output.ogg",
		copyDir);

	log_trace ("render_thread");

	log_info ("Executing: %s", copyCmd);
	int ret = system (copyCmd);

	if (ret != 0) {
		// TODO notify of error
		log_error ("Rendering failed, code %d", ret);
		free (copyDir);
		free (copyCmd);
		free (outFile);
		pthread_exit (NULL);
	}

	struct llFinishedRender* newRender = malloc (sizeof (struct llFinishedRender));
	newRender->finishedPath = outFile;
	newRender->channelId = channelId;
	newRender->next = NULL;

	pthread_mutex_lock (&finishedRendersMutex);
	if (finishedRenders == NULL) {
		finishedRenders = newRender;
	} else {
		struct llFinishedRender* wander = finishedRenders;
		while (wander->next != NULL) wander = wander->next;
		wander->next = newRender;
	}
	pthread_mutex_unlock (&finishedRendersMutex);

	log_trace ("Render ready to be served! Exiting...");
	free (copyDir);
	free (copyCmd);
	pthread_exit (NULL);
}

void bpd_interaction_vgmrender_cmd (struct discord* client, const struct discord_interaction* event) {
	log_info ("interaction_vgmrender_cmd");

	if (!event->data || !event->data->options) {
		// TODO refactor into error function
		char buf[1024];
		snprintf (buf, sizeof (buf) / sizeof(buf[0]),
			"User input is missing? Data: %p, Options: %p",
			(void*)event->data,
			(void*)event->data->options);
		log_error (buf);

		char buf2[sizeof (buf) / sizeof(buf[0])];
		//char buf2[(sizeof (buf) / sizeof(buf[0])) + (sizeof (ERROR_PREFIX) / sizeof(ERROR_PREFIX[0]))];
		snprintf (buf2, sizeof (buf2) / sizeof(buf2[0]),
			"%s%s",
			ERROR_PREFIX,
			buf);
		struct discord_interaction_response paramError = {
			.type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
			.data = &(struct discord_interaction_callback_data) {
				.content = buf2,
			},
		};
		discord_create_interaction_response (client, event->id, event->token, &paramError, NULL);
	}

	struct discord_interaction_response paramInitial = {
		.type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
		.data = &(struct discord_interaction_callback_data) {
			.content = "Rendering in process..."
		}
	};
	discord_create_interaction_response (client, event->id, event->token, &paramInitial, NULL);

	char* attachmentId = NULL;
	for (int i = 0; i < event->data->options->size; ++i) {
		char* name = event->data->options->array[i].name;
		json_char* value = event->data->options->array[i].value;
		log_debug ("Argument %s, Type %d, Value %s", name, event->data->options->array[i].type, value);
		if (strcmp (name, "vgm") == 0) {
			attachmentId = value;
		}
	}

	// TODO check for attachmentId still being unset

	struct json_object* snowflakeMapping = json_tokener_parse (event->data->resolved->attachments);
	struct json_object* attachmentDetails;
	if (!json_object_object_get_ex (snowflakeMapping, attachmentId, &attachmentDetails)) {
		// TODO notify of error
		log_error ("%s not found in %s", attachmentId, event->data->resolved->attachments);
		return;
	}

	struct json_object* attachmentUrlObj;
	if (!json_object_object_get_ex (attachmentDetails, "url", &attachmentUrlObj)) {
		// TODO notify of error
		log_error ("%s not found in %s", "url", event->data->resolved->attachments);
		return;
	}

	const char* attachmentUrl = json_object_get_string (attachmentUrlObj);
	log_info ("VGM file: %s", attachmentUrl);

	char workdirTemplate[] = "/tmp/dbt-render.XXXXXX";
	const char* workdir = mkdtemp (workdirTemplate);
	if (workdir == NULL) {
		// TODO notify of error
		log_error ("Failed to create temp dir");
		return;
	}

	// TODO this is awful, write all of this in proper C, I just CBA rn
	char renderCmd[1024];
	snprintf (renderCmd, sizeof (renderCmd) / sizeof (renderCmd[0]),
		"'%s' '%s' '%s'",
		DLAR_SCRIPT,
		attachmentUrl,
		workdir);

	size_t len = strlen (attachmentUrl);
	log_debug ("malloc(%zu)", len);
	char* threadcopyUrl = malloc (++len);
	strcpy (threadcopyUrl, attachmentUrl);

	len = strlen (workdir);
	log_debug ("malloc(%zu)", len);
	char* threadcopyDir = malloc (++len);
	strcpy (threadcopyDir, workdir);

	len = strlen (renderCmd);
	log_debug ("malloc(%zu)", len);
	char* threadcopyCmd = malloc (++len);
	strcpy (threadcopyCmd, renderCmd);

	struct renderthreadData threadData = {
		.workDir = threadcopyDir,
		.renderCmd = threadcopyCmd,
		.channelId = event->channel_id,
	};

	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_t tid;
	if (pthread_create (&tid, &attr, render_thread, (void*)&threadData) != 0) {
		log_error ("Failed to start render thread");
		return;
	}

	struct discord_edit_original_interaction_response paramEdit = {
		.content = "Rendering your request! Result should be sent soon…",
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);

	// something seems to be flawed with the way the data is passed to the render thread, seems to be made invalid
	// worked around here by waiting abit before returning
	sleep (1);
}
